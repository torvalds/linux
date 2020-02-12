// SPDX-License-Identifier: GPL-2.0
/*
 * Turris Mox module configuration bus driver
 *
 * Copyright (C) 2019 Marek Behun <marek.behun@nic.cz>
 */

#include <dt-bindings/bus/moxtet.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moxtet.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/spi/spi.h>

/*
 * @name:	module name for sysfs
 * @hwirq_base:	base index for IRQ for this module (-1 if no IRQs)
 * @nirqs:	how many interrupts does the shift register provide
 * @desc:	module description for kernel log
 */
static const struct {
	const char *name;
	int hwirq_base;
	int nirqs;
	const char *desc;
} mox_module_table[] = {
	/* do not change order of this array! */
	{ NULL,		 0,			0, NULL },
	{ "sfp",	-1,			0, "MOX D (SFP cage)" },
	{ "pci",	MOXTET_IRQ_PCI,		1, "MOX B (Mini-PCIe)" },
	{ "topaz",	MOXTET_IRQ_TOPAZ,	1, "MOX C (4 port switch)" },
	{ "peridot",	MOXTET_IRQ_PERIDOT(0),	1, "MOX E (8 port switch)" },
	{ "usb3",	MOXTET_IRQ_USB3,	2, "MOX F (USB 3.0)" },
	{ "pci-bridge",	-1,			0, "MOX G (Mini-PCIe bridge)" },
};

static inline bool mox_module_known(unsigned int id)
{
	return id >= TURRIS_MOX_MODULE_FIRST && id <= TURRIS_MOX_MODULE_LAST;
}

static inline const char *mox_module_name(unsigned int id)
{
	if (mox_module_known(id))
		return mox_module_table[id].name;
	else
		return "unknown";
}

#define DEF_MODULE_ATTR(name, fmt, ...)					\
static ssize_t								\
module_##name##_show(struct device *dev, struct device_attribute *a,	\
		     char *buf)						\
{									\
	struct moxtet_device *mdev = to_moxtet_device(dev);		\
	return sprintf(buf, (fmt), __VA_ARGS__);			\
}									\
static DEVICE_ATTR_RO(module_##name)

DEF_MODULE_ATTR(id, "0x%x\n", mdev->id);
DEF_MODULE_ATTR(name, "%s\n", mox_module_name(mdev->id));
DEF_MODULE_ATTR(description, "%s\n",
		mox_module_known(mdev->id) ? mox_module_table[mdev->id].desc
					   : "");

static struct attribute *moxtet_dev_attrs[] = {
	&dev_attr_module_id.attr,
	&dev_attr_module_name.attr,
	&dev_attr_module_description.attr,
	NULL,
};

static const struct attribute_group moxtet_dev_group = {
	.attrs = moxtet_dev_attrs,
};

static const struct attribute_group *moxtet_dev_groups[] = {
	&moxtet_dev_group,
	NULL,
};

static int moxtet_match(struct device *dev, struct device_driver *drv)
{
	struct moxtet_device *mdev = to_moxtet_device(dev);
	struct moxtet_driver *tdrv = to_moxtet_driver(drv);
	const enum turris_mox_module_id *t;

	if (of_driver_match_device(dev, drv))
		return 1;

	if (!tdrv->id_table)
		return 0;

	for (t = tdrv->id_table; *t; ++t)
		if (*t == mdev->id)
			return 1;

	return 0;
}

static struct bus_type moxtet_bus_type = {
	.name		= "moxtet",
	.dev_groups	= moxtet_dev_groups,
	.match		= moxtet_match,
};

int __moxtet_register_driver(struct module *owner,
			     struct moxtet_driver *mdrv)
{
	mdrv->driver.owner = owner;
	mdrv->driver.bus = &moxtet_bus_type;
	return driver_register(&mdrv->driver);
}
EXPORT_SYMBOL_GPL(__moxtet_register_driver);

static int moxtet_dev_check(struct device *dev, void *data)
{
	struct moxtet_device *mdev = to_moxtet_device(dev);
	struct moxtet_device *new_dev = data;

	if (mdev->moxtet == new_dev->moxtet && mdev->id == new_dev->id &&
	    mdev->idx == new_dev->idx)
		return -EBUSY;
	return 0;
}

static void moxtet_dev_release(struct device *dev)
{
	struct moxtet_device *mdev = to_moxtet_device(dev);

	put_device(mdev->moxtet->dev);
	kfree(mdev);
}

static struct moxtet_device *
moxtet_alloc_device(struct moxtet *moxtet)
{
	struct moxtet_device *dev;

	if (!get_device(moxtet->dev))
		return NULL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		put_device(moxtet->dev);
		return NULL;
	}

	dev->moxtet = moxtet;
	dev->dev.parent = moxtet->dev;
	dev->dev.bus = &moxtet_bus_type;
	dev->dev.release = moxtet_dev_release;

	device_initialize(&dev->dev);

	return dev;
}

static int moxtet_add_device(struct moxtet_device *dev)
{
	static DEFINE_MUTEX(add_mutex);
	int ret;

	if (dev->idx >= TURRIS_MOX_MAX_MODULES || dev->id > 0xf)
		return -EINVAL;

	dev_set_name(&dev->dev, "moxtet-%s.%u", mox_module_name(dev->id),
		     dev->idx);

	mutex_lock(&add_mutex);

	ret = bus_for_each_dev(&moxtet_bus_type, NULL, dev,
			       moxtet_dev_check);
	if (ret)
		goto done;

	ret = device_add(&dev->dev);
	if (ret < 0)
		dev_err(dev->moxtet->dev, "can't add %s, status %d\n",
			dev_name(dev->moxtet->dev), ret);

done:
	mutex_unlock(&add_mutex);
	return ret;
}

static int __unregister(struct device *dev, void *null)
{
	if (dev->of_node) {
		of_node_clear_flag(dev->of_node, OF_POPULATED);
		of_node_put(dev->of_node);
	}

	device_unregister(dev);

	return 0;
}

static struct moxtet_device *
of_register_moxtet_device(struct moxtet *moxtet, struct device_node *nc)
{
	struct moxtet_device *dev;
	u32 val;
	int ret;

	dev = moxtet_alloc_device(moxtet);
	if (!dev) {
		dev_err(moxtet->dev,
			"Moxtet device alloc error for %pOF\n", nc);
		return ERR_PTR(-ENOMEM);
	}

	ret = of_property_read_u32(nc, "reg", &val);
	if (ret) {
		dev_err(moxtet->dev, "%pOF has no valid 'reg' property (%d)\n",
			nc, ret);
		goto err_put;
	}

	dev->idx = val;

	if (dev->idx >= TURRIS_MOX_MAX_MODULES) {
		dev_err(moxtet->dev, "%pOF Moxtet address 0x%x out of range\n",
			nc, dev->idx);
		ret = -EINVAL;
		goto err_put;
	}

	dev->id = moxtet->modules[dev->idx];

	if (!dev->id) {
		dev_err(moxtet->dev, "%pOF Moxtet address 0x%x is empty\n", nc,
			dev->idx);
		ret = -ENODEV;
		goto err_put;
	}

	of_node_get(nc);
	dev->dev.of_node = nc;

	ret = moxtet_add_device(dev);
	if (ret) {
		dev_err(moxtet->dev,
			"Moxtet device register error for %pOF\n", nc);
		of_node_put(nc);
		goto err_put;
	}

	return dev;

err_put:
	put_device(&dev->dev);
	return ERR_PTR(ret);
}

static void of_register_moxtet_devices(struct moxtet *moxtet)
{
	struct moxtet_device *dev;
	struct device_node *nc;

	if (!moxtet->dev->of_node)
		return;

	for_each_available_child_of_node(moxtet->dev->of_node, nc) {
		if (of_node_test_and_set_flag(nc, OF_POPULATED))
			continue;
		dev = of_register_moxtet_device(moxtet, nc);
		if (IS_ERR(dev)) {
			dev_warn(moxtet->dev,
				 "Failed to create Moxtet device for %pOF\n",
				 nc);
			of_node_clear_flag(nc, OF_POPULATED);
		}
	}
}

static void
moxtet_register_devices_from_topology(struct moxtet *moxtet)
{
	struct moxtet_device *dev;
	int i, ret;

	for (i = 0; i < moxtet->count; ++i) {
		dev = moxtet_alloc_device(moxtet);
		if (!dev) {
			dev_err(moxtet->dev, "Moxtet device %u alloc error\n",
				i);
			continue;
		}

		dev->idx = i;
		dev->id = moxtet->modules[i];

		ret = moxtet_add_device(dev);
		if (ret && ret != -EBUSY) {
			put_device(&dev->dev);
			dev_err(moxtet->dev,
				"Moxtet device %u register error: %i\n", i,
				ret);
		}
	}
}

/*
 * @nsame:	how many modules with same id are already in moxtet->modules
 */
static int moxtet_set_irq(struct moxtet *moxtet, int idx, int id, int nsame)
{
	int i, first;
	struct moxtet_irqpos *pos;

	first = mox_module_table[id].hwirq_base +
		nsame * mox_module_table[id].nirqs;

	if (first + mox_module_table[id].nirqs > MOXTET_NIRQS)
		return -EINVAL;

	for (i = 0; i < mox_module_table[id].nirqs; ++i) {
		pos = &moxtet->irq.position[first + i];
		pos->idx = idx;
		pos->bit = i;
		moxtet->irq.exists |= BIT(first + i);
	}

	return 0;
}

static int moxtet_find_topology(struct moxtet *moxtet)
{
	u8 buf[TURRIS_MOX_MAX_MODULES];
	int cnts[TURRIS_MOX_MODULE_LAST];
	int i, ret;

	memset(cnts, 0, sizeof(cnts));

	ret = spi_read(to_spi_device(moxtet->dev), buf, TURRIS_MOX_MAX_MODULES);
	if (ret < 0)
		return ret;

	if (buf[0] == TURRIS_MOX_CPU_ID_EMMC) {
		dev_info(moxtet->dev, "Found MOX A (eMMC CPU) module\n");
	} else if (buf[0] == TURRIS_MOX_CPU_ID_SD) {
		dev_info(moxtet->dev, "Found MOX A (CPU) module\n");
	} else {
		dev_err(moxtet->dev, "Invalid Turris MOX A CPU module 0x%02x\n",
			buf[0]);
		return -ENODEV;
	}

	moxtet->count = 0;

	for (i = 1; i < TURRIS_MOX_MAX_MODULES; ++i) {
		int id;

		if (buf[i] == 0xff)
			break;

		id = buf[i] & 0xf;

		moxtet->modules[i-1] = id;
		++moxtet->count;

		if (mox_module_known(id)) {
			dev_info(moxtet->dev, "Found %s module\n",
				 mox_module_table[id].desc);

			if (moxtet_set_irq(moxtet, i-1, id, cnts[id]++) < 0)
				dev_err(moxtet->dev,
					"  Cannot set IRQ for module %s\n",
					mox_module_table[id].desc);
		} else {
			dev_warn(moxtet->dev,
				 "Unknown Moxtet module found (ID 0x%02x)\n",
				 id);
		}
	}

	return 0;
}

static int moxtet_spi_read(struct moxtet *moxtet, u8 *buf)
{
	struct spi_transfer xfer = {
		.rx_buf = buf,
		.tx_buf = moxtet->tx,
		.len = moxtet->count + 1
	};
	int ret;

	mutex_lock(&moxtet->lock);

	ret = spi_sync_transfer(to_spi_device(moxtet->dev), &xfer, 1);

	mutex_unlock(&moxtet->lock);

	return ret;
}

int moxtet_device_read(struct device *dev)
{
	struct moxtet_device *mdev = to_moxtet_device(dev);
	struct moxtet *moxtet = mdev->moxtet;
	u8 buf[TURRIS_MOX_MAX_MODULES];
	int ret;

	if (mdev->idx >= moxtet->count)
		return -EINVAL;

	ret = moxtet_spi_read(moxtet, buf);
	if (ret < 0)
		return ret;

	return buf[mdev->idx + 1] >> 4;
}
EXPORT_SYMBOL_GPL(moxtet_device_read);

int moxtet_device_write(struct device *dev, u8 val)
{
	struct moxtet_device *mdev = to_moxtet_device(dev);
	struct moxtet *moxtet = mdev->moxtet;
	int ret;

	if (mdev->idx >= moxtet->count)
		return -EINVAL;

	mutex_lock(&moxtet->lock);

	moxtet->tx[moxtet->count - mdev->idx] = val;

	ret = spi_write(to_spi_device(moxtet->dev), moxtet->tx,
			moxtet->count + 1);

	mutex_unlock(&moxtet->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(moxtet_device_write);

int moxtet_device_written(struct device *dev)
{
	struct moxtet_device *mdev = to_moxtet_device(dev);
	struct moxtet *moxtet = mdev->moxtet;

	if (mdev->idx >= moxtet->count)
		return -EINVAL;

	return moxtet->tx[moxtet->count - mdev->idx];
}
EXPORT_SYMBOL_GPL(moxtet_device_written);

#ifdef CONFIG_DEBUG_FS
static int moxtet_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return nonseekable_open(inode, file);
}

static ssize_t input_read(struct file *file, char __user *buf, size_t len,
			  loff_t *ppos)
{
	struct moxtet *moxtet = file->private_data;
	u8 bin[TURRIS_MOX_MAX_MODULES];
	u8 hex[sizeof(buf) * 2 + 1];
	int ret, n;

	ret = moxtet_spi_read(moxtet, bin);
	if (ret < 0)
		return ret;

	n = moxtet->count + 1;
	bin2hex(hex, bin, n);

	hex[2*n] = '\n';

	return simple_read_from_buffer(buf, len, ppos, hex, 2*n + 1);
}

static const struct file_operations input_fops = {
	.owner	= THIS_MODULE,
	.open	= moxtet_debug_open,
	.read	= input_read,
	.llseek	= no_llseek,
};

static ssize_t output_read(struct file *file, char __user *buf, size_t len,
			   loff_t *ppos)
{
	struct moxtet *moxtet = file->private_data;
	u8 hex[TURRIS_MOX_MAX_MODULES * 2 + 1];
	u8 *p = hex;
	int i;

	mutex_lock(&moxtet->lock);

	for (i = 0; i < moxtet->count; ++i)
		p = hex_byte_pack(p, moxtet->tx[moxtet->count - i]);

	mutex_unlock(&moxtet->lock);

	*p++ = '\n';

	return simple_read_from_buffer(buf, len, ppos, hex, p - hex);
}

static ssize_t output_write(struct file *file, const char __user *buf,
			    size_t len, loff_t *ppos)
{
	struct moxtet *moxtet = file->private_data;
	u8 bin[TURRIS_MOX_MAX_MODULES];
	u8 hex[sizeof(bin) * 2 + 1];
	ssize_t res;
	loff_t dummy = 0;
	int err, i;

	if (len > 2 * moxtet->count + 1 || len < 2 * moxtet->count)
		return -EINVAL;

	res = simple_write_to_buffer(hex, sizeof(hex), &dummy, buf, len);
	if (res < 0)
		return res;

	if (len % 2 == 1 && hex[len - 1] != '\n')
		return -EINVAL;

	err = hex2bin(bin, hex, moxtet->count);
	if (err < 0)
		return -EINVAL;

	mutex_lock(&moxtet->lock);

	for (i = 0; i < moxtet->count; ++i)
		moxtet->tx[moxtet->count - i] = bin[i];

	err = spi_write(to_spi_device(moxtet->dev), moxtet->tx,
			moxtet->count + 1);

	mutex_unlock(&moxtet->lock);

	return err < 0 ? err : len;
}

static const struct file_operations output_fops = {
	.owner	= THIS_MODULE,
	.open	= moxtet_debug_open,
	.read	= output_read,
	.write	= output_write,
	.llseek	= no_llseek,
};

static int moxtet_register_debugfs(struct moxtet *moxtet)
{
	struct dentry *root, *entry;

	root = debugfs_create_dir("moxtet", NULL);

	if (IS_ERR(root))
		return PTR_ERR(root);

	entry = debugfs_create_file_unsafe("input", 0444, root, moxtet,
					   &input_fops);
	if (IS_ERR(entry))
		goto err_remove;

	entry = debugfs_create_file_unsafe("output", 0644, root, moxtet,
					   &output_fops);
	if (IS_ERR(entry))
		goto err_remove;

	moxtet->debugfs_root = root;

	return 0;
err_remove:
	debugfs_remove_recursive(root);
	return PTR_ERR(entry);
}

static void moxtet_unregister_debugfs(struct moxtet *moxtet)
{
	debugfs_remove_recursive(moxtet->debugfs_root);
}
#else
static inline int moxtet_register_debugfs(struct moxtet *moxtet)
{
	return 0;
}

static inline void moxtet_unregister_debugfs(struct moxtet *moxtet)
{
}
#endif

static int moxtet_irq_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hw)
{
	struct moxtet *moxtet = d->host_data;

	if (hw >= MOXTET_NIRQS || !(moxtet->irq.exists & BIT(hw))) {
		dev_err(moxtet->dev, "Invalid hw irq number\n");
		return -EINVAL;
	}

	irq_set_chip_data(irq, d->host_data);
	irq_set_chip_and_handler(irq, &moxtet->irq.chip, handle_level_irq);

	return 0;
}

static int moxtet_irq_domain_xlate(struct irq_domain *d,
				   struct device_node *ctrlr,
				   const u32 *intspec, unsigned int intsize,
				   unsigned long *out_hwirq,
				   unsigned int *out_type)
{
	struct moxtet *moxtet = d->host_data;
	int irq;

	if (WARN_ON(intsize < 1))
		return -EINVAL;

	irq = intspec[0];

	if (irq >= MOXTET_NIRQS || !(moxtet->irq.exists & BIT(irq)))
		return -EINVAL;

	*out_hwirq = irq;
	*out_type = IRQ_TYPE_NONE;
	return 0;
}

static const struct irq_domain_ops moxtet_irq_domain = {
	.map = moxtet_irq_domain_map,
	.xlate = moxtet_irq_domain_xlate,
};

static void moxtet_irq_mask(struct irq_data *d)
{
	struct moxtet *moxtet = irq_data_get_irq_chip_data(d);

	moxtet->irq.masked |= BIT(d->hwirq);
}

static void moxtet_irq_unmask(struct irq_data *d)
{
	struct moxtet *moxtet = irq_data_get_irq_chip_data(d);

	moxtet->irq.masked &= ~BIT(d->hwirq);
}

static void moxtet_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct moxtet *moxtet = irq_data_get_irq_chip_data(d);
	struct moxtet_irqpos *pos = &moxtet->irq.position[d->hwirq];
	int id;

	id = moxtet->modules[pos->idx];

	seq_printf(p, " moxtet-%s.%i#%i", mox_module_name(id), pos->idx,
		   pos->bit);
}

static const struct irq_chip moxtet_irq_chip = {
	.name			= "moxtet",
	.irq_mask		= moxtet_irq_mask,
	.irq_unmask		= moxtet_irq_unmask,
	.irq_print_chip		= moxtet_irq_print_chip,
};

static int moxtet_irq_read(struct moxtet *moxtet, unsigned long *map)
{
	struct moxtet_irqpos *pos = moxtet->irq.position;
	u8 buf[TURRIS_MOX_MAX_MODULES];
	int i, ret;

	ret = moxtet_spi_read(moxtet, buf);
	if (ret < 0)
		return ret;

	*map = 0;

	for_each_set_bit(i, &moxtet->irq.exists, MOXTET_NIRQS) {
		if (!(buf[pos[i].idx + 1] & BIT(4 + pos[i].bit)))
			set_bit(i, map);
	}

	return 0;
}

static irqreturn_t moxtet_irq_thread_fn(int irq, void *data)
{
	struct moxtet *moxtet = data;
	unsigned long set;
	int nhandled = 0, i, sub_irq, ret;

	ret = moxtet_irq_read(moxtet, &set);
	if (ret < 0)
		goto out;

	set &= ~moxtet->irq.masked;

	do {
		for_each_set_bit(i, &set, MOXTET_NIRQS) {
			sub_irq = irq_find_mapping(moxtet->irq.domain, i);
			handle_nested_irq(sub_irq);
			dev_dbg(moxtet->dev, "%i irq\n", i);
			++nhandled;
		}

		ret = moxtet_irq_read(moxtet, &set);
		if (ret < 0)
			goto out;

		set &= ~moxtet->irq.masked;
	} while (set);

out:
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static void moxtet_irq_free(struct moxtet *moxtet)
{
	int i, irq;

	for (i = 0; i < MOXTET_NIRQS; ++i) {
		if (moxtet->irq.exists & BIT(i)) {
			irq = irq_find_mapping(moxtet->irq.domain, i);
			irq_dispose_mapping(irq);
		}
	}

	irq_domain_remove(moxtet->irq.domain);
}

static int moxtet_irq_setup(struct moxtet *moxtet)
{
	int i, ret;

	moxtet->irq.domain = irq_domain_add_simple(moxtet->dev->of_node,
						   MOXTET_NIRQS, 0,
						   &moxtet_irq_domain, moxtet);
	if (moxtet->irq.domain == NULL) {
		dev_err(moxtet->dev, "Could not add IRQ domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < MOXTET_NIRQS; ++i)
		if (moxtet->irq.exists & BIT(i))
			irq_create_mapping(moxtet->irq.domain, i);

	moxtet->irq.chip = moxtet_irq_chip;
	moxtet->irq.masked = ~0;

	ret = request_threaded_irq(moxtet->dev_irq, NULL, moxtet_irq_thread_fn,
				   IRQF_ONESHOT, "moxtet", moxtet);
	if (ret < 0)
		goto err_free;

	return 0;

err_free:
	moxtet_irq_free(moxtet);
	return ret;
}

static int moxtet_probe(struct spi_device *spi)
{
	struct moxtet *moxtet;
	int ret;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	moxtet = devm_kzalloc(&spi->dev, sizeof(struct moxtet),
			      GFP_KERNEL);
	if (!moxtet)
		return -ENOMEM;

	moxtet->dev = &spi->dev;
	spi_set_drvdata(spi, moxtet);

	mutex_init(&moxtet->lock);

	moxtet->dev_irq = of_irq_get(moxtet->dev->of_node, 0);
	if (moxtet->dev_irq == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (moxtet->dev_irq <= 0) {
		dev_err(moxtet->dev, "No IRQ resource found\n");
		return -ENXIO;
	}

	ret = moxtet_find_topology(moxtet);
	if (ret < 0)
		return ret;

	if (moxtet->irq.exists) {
		ret = moxtet_irq_setup(moxtet);
		if (ret < 0)
			return ret;
	}

	of_register_moxtet_devices(moxtet);
	moxtet_register_devices_from_topology(moxtet);

	ret = moxtet_register_debugfs(moxtet);
	if (ret < 0)
		dev_warn(moxtet->dev, "Failed creating debugfs entries: %i\n",
			 ret);

	return 0;
}

static int moxtet_remove(struct spi_device *spi)
{
	struct moxtet *moxtet = spi_get_drvdata(spi);

	free_irq(moxtet->dev_irq, moxtet);

	moxtet_irq_free(moxtet);

	moxtet_unregister_debugfs(moxtet);

	device_for_each_child(moxtet->dev, NULL, __unregister);

	mutex_destroy(&moxtet->lock);

	return 0;
}

static const struct of_device_id moxtet_dt_ids[] = {
	{ .compatible = "cznic,moxtet" },
	{},
};
MODULE_DEVICE_TABLE(of, moxtet_dt_ids);

static struct spi_driver moxtet_spi_driver = {
	.driver = {
		.name		= "moxtet",
		.of_match_table = moxtet_dt_ids,
	},
	.probe		= moxtet_probe,
	.remove		= moxtet_remove,
};

static int __init moxtet_init(void)
{
	int ret;

	ret = bus_register(&moxtet_bus_type);
	if (ret < 0) {
		pr_err("moxtet bus registration failed: %d\n", ret);
		goto error;
	}

	ret = spi_register_driver(&moxtet_spi_driver);
	if (ret < 0) {
		pr_err("moxtet spi driver registration failed: %d\n", ret);
		goto error_bus;
	}

	return 0;

error_bus:
	bus_unregister(&moxtet_bus_type);
error:
	return ret;
}
postcore_initcall_sync(moxtet_init);

static void __exit moxtet_exit(void)
{
	spi_unregister_driver(&moxtet_spi_driver);
	bus_unregister(&moxtet_bus_type);
}
module_exit(moxtet_exit);

MODULE_AUTHOR("Marek Behun <marek.behun@nic.cz>");
MODULE_DESCRIPTION("CZ.NIC's Turris Mox module configuration bus");
MODULE_LICENSE("GPL v2");
