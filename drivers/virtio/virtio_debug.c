// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/debugfs.h>

static struct dentry *virtio_debugfs_dir;

static int virtio_debug_device_features_show(struct seq_file *s, void *data)
{
	struct virtio_device *dev = s->private;
	u64 device_features;
	unsigned int i;

	device_features = dev->config->get_features(dev);
	for (i = 0; i < BITS_PER_LONG_LONG; i++) {
		if (device_features & (1ULL << i))
			seq_printf(s, "%u\n", i);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(virtio_debug_device_features);

static int virtio_debug_filter_features_show(struct seq_file *s, void *data)
{
	struct virtio_device *dev = s->private;
	unsigned int i;

	for (i = 0; i < BITS_PER_LONG_LONG; i++) {
		if (dev->debugfs_filter_features & (1ULL << i))
			seq_printf(s, "%u\n", i);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(virtio_debug_filter_features);

static int virtio_debug_filter_features_clear(void *data, u64 val)
{
	struct virtio_device *dev = data;

	if (val == 1)
		dev->debugfs_filter_features = 0;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(virtio_debug_filter_features_clear_fops, NULL,
			 virtio_debug_filter_features_clear, "%llu\n");

static int virtio_debug_filter_feature_add(void *data, u64 val)
{
	struct virtio_device *dev = data;

	if (val >= BITS_PER_LONG_LONG)
		return -EINVAL;
	dev->debugfs_filter_features |= BIT_ULL_MASK(val);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(virtio_debug_filter_feature_add_fops, NULL,
			 virtio_debug_filter_feature_add, "%llu\n");

static int virtio_debug_filter_feature_del(void *data, u64 val)
{
	struct virtio_device *dev = data;

	if (val >= BITS_PER_LONG_LONG)
		return -EINVAL;
	dev->debugfs_filter_features &= ~BIT_ULL_MASK(val);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(virtio_debug_filter_feature_del_fops, NULL,
			 virtio_debug_filter_feature_del, "%llu\n");

void virtio_debug_device_init(struct virtio_device *dev)
{
	dev->debugfs_dir = debugfs_create_dir(dev_name(&dev->dev),
					      virtio_debugfs_dir);
	debugfs_create_file("device_features", 0400, dev->debugfs_dir, dev,
			    &virtio_debug_device_features_fops);
	debugfs_create_file("filter_features", 0400, dev->debugfs_dir, dev,
			    &virtio_debug_filter_features_fops);
	debugfs_create_file("filter_features_clear", 0200, dev->debugfs_dir, dev,
			    &virtio_debug_filter_features_clear_fops);
	debugfs_create_file("filter_feature_add", 0200, dev->debugfs_dir, dev,
			    &virtio_debug_filter_feature_add_fops);
	debugfs_create_file("filter_feature_del", 0200, dev->debugfs_dir, dev,
			    &virtio_debug_filter_feature_del_fops);
}
EXPORT_SYMBOL_GPL(virtio_debug_device_init);

void virtio_debug_device_filter_features(struct virtio_device *dev)
{
	dev->features &= ~dev->debugfs_filter_features;
}
EXPORT_SYMBOL_GPL(virtio_debug_device_filter_features);

void virtio_debug_device_exit(struct virtio_device *dev)
{
	debugfs_remove_recursive(dev->debugfs_dir);
}
EXPORT_SYMBOL_GPL(virtio_debug_device_exit);

void virtio_debug_init(void)
{
	virtio_debugfs_dir = debugfs_create_dir("virtio", NULL);
}
EXPORT_SYMBOL_GPL(virtio_debug_init);

void virtio_debug_exit(void)
{
	debugfs_remove_recursive(virtio_debugfs_dir);
}
EXPORT_SYMBOL_GPL(virtio_debug_exit);
