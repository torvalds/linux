// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/debugfs.h>
#include <linux/interconnect.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "internal.h"

/*
 * This can be dangerous, therefore don't provide any real compile time
 * configuration option for this feature.
 * People who want to use this will need to modify the source code directly.
 */
#undef INTERCONNECT_ALLOW_WRITE_DEBUGFS

#if defined(INTERCONNECT_ALLOW_WRITE_DEBUGFS) && defined(CONFIG_DEBUG_FS)

static LIST_HEAD(debugfs_paths);
static DEFINE_MUTEX(debugfs_lock);

static struct platform_device *pdev;
static struct icc_path *cur_path;

static char *src_node;
static char *dst_node;
static u32 avg_bw;
static u32 peak_bw;
static u32 tag;

struct debugfs_path {
	const char *src;
	const char *dst;
	struct icc_path *path;
	struct list_head list;
};

static ssize_t icc_node_read(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char **node = file->private_data;
	char *copy;
	size_t len;
	ssize_t ret;

	scoped_guard(mutex, &debugfs_lock) {
		copy = kstrdup(*node ?: "", GFP_KERNEL);
	}
	if (!copy)
		return -ENOMEM;

	len = strlen(copy);
	copy[len++] = '\n';
	ret = simple_read_from_buffer(user_buf, count, ppos, copy, len);
	kfree(copy);
	return ret;
}

static ssize_t icc_node_write(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char **node = file->private_data;
	char *old, *new;

	if (*ppos)
		return -EINVAL;
	if (count + 1 > PAGE_SIZE)
		return -E2BIG;

	new = memdup_user_nul(user_buf, count);
	if (IS_ERR(new))
		return PTR_ERR(new);
	strim(new);

	scoped_guard(mutex, &debugfs_lock) {
		old = *node;
		*node = new;
	}

	kfree(old);
	return count;
}

static const struct file_operations icc_node_fops = {
	.open = simple_open,
	.read = icc_node_read,
	.write = icc_node_write,
	.llseek = default_llseek,
};

static struct icc_path *get_path(const char *src, const char *dst)
{
	struct debugfs_path *path;

	list_for_each_entry(path, &debugfs_paths, list) {
		if (!strcmp(path->src, src) && !strcmp(path->dst, dst))
			return path->path;
	}

	return NULL;
}

static int icc_get_set(void *data, u64 val)
{
	struct debugfs_path *debugfs_path;
	char *src, *dst;
	int ret = 0;

	guard(mutex)(&debugfs_lock);

	/*
	 * If we've already looked up a path, then use the existing one instead
	 * of calling icc_get() again. This allows for updating previous BW
	 * votes when "get" is written to multiple times for multiple paths.
	 */
	cur_path = get_path(src_node, dst_node);
	if (cur_path)
		goto out;

	src = kstrdup(src_node, GFP_KERNEL);
	dst = kstrdup(dst_node, GFP_KERNEL);

	if (!src || !dst) {
		ret = -ENOMEM;
		goto err_free;
	}

	cur_path = icc_get(&pdev->dev, src, dst);
	if (IS_ERR(cur_path)) {
		ret = PTR_ERR(cur_path);
		goto err_free;
	}

	debugfs_path = kzalloc_obj(*debugfs_path);
	if (!debugfs_path) {
		ret = -ENOMEM;
		goto err_put;
	}

	debugfs_path->path = cur_path;
	debugfs_path->src = src;
	debugfs_path->dst = dst;
	list_add_tail(&debugfs_path->list, &debugfs_paths);

	goto out;

err_put:
	icc_put(cur_path);
err_free:
	kfree(src);
	kfree(dst);
out:
	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(icc_get_fops, NULL, icc_get_set, "%llu\n");

static int icc_commit_set(void *data, u64 val)
{
	int ret;

	guard(mutex)(&debugfs_lock);

	if (!cur_path) {
		ret = -EINVAL;
		goto out;
	}

	if (IS_ERR(cur_path)) {
		ret = PTR_ERR(cur_path);
		goto out;
	}

	icc_set_tag(cur_path, tag);
	ret = icc_set_bw(cur_path, avg_bw, peak_bw);
out:
	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(icc_commit_fops, NULL, icc_commit_set, "%llu\n");

int icc_debugfs_client_init(struct dentry *icc_dir)
{
	struct dentry *client_dir;
	int ret;

	pdev = platform_device_alloc("icc-debugfs-client", PLATFORM_DEVID_NONE);
	if (!pdev)
		return -ENOMEM;

	ret = platform_device_add(pdev);
	if (ret) {
		pr_err("%s: failed to add platform device: %d\n", __func__, ret);
		platform_device_put(pdev);
		return ret;
	}

	src_node = kstrdup("", GFP_KERNEL);
	dst_node = kstrdup("", GFP_KERNEL);
	if (!src_node || !dst_node) {
		kfree(dst_node);
		kfree(src_node);
		return -ENOMEM;
	}

	client_dir = debugfs_create_dir("test_client", icc_dir);

	debugfs_create_file("src_node", 0600, client_dir, &src_node,
			    &icc_node_fops);
	debugfs_create_file("dst_node", 0600, client_dir, &dst_node,
			    &icc_node_fops);
	debugfs_create_file("get", 0200, client_dir, NULL, &icc_get_fops);
	debugfs_create_u32("avg_bw", 0600, client_dir, &avg_bw);
	debugfs_create_u32("peak_bw", 0600, client_dir, &peak_bw);
	debugfs_create_u32("tag", 0600, client_dir, &tag);
	debugfs_create_file("commit", 0200, client_dir, NULL, &icc_commit_fops);

	return 0;
}

#else

int icc_debugfs_client_init(struct dentry *icc_dir)
{
	return 0;
}

#endif
