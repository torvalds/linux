// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debugfs interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/debugfs.h>
#include <linux/crc32.h>

#include "debug.h"
#include "wfx.h"
#include "main.h"

#define CREATE_TRACE_POINTS
#include "traces.h"

static const struct trace_print_flags hif_msg_print_map[] = {
	hif_msg_list,
};

static const struct trace_print_flags hif_mib_print_map[] = {
	hif_mib_list,
};

static const struct trace_print_flags wfx_reg_print_map[] = {
	wfx_reg_list,
};

static const char *get_symbol(unsigned long val,
		const struct trace_print_flags *symbol_array)
{
	int i;

	for (i = 0; symbol_array[i].mask != -1; i++) {
		if (val == symbol_array[i].mask)
			return symbol_array[i].name;
	}

	return "unknown";
}

const char *get_hif_name(unsigned long id)
{
	return get_symbol(id, hif_msg_print_map);
}

const char *get_mib_name(unsigned long id)
{
	return get_symbol(id, hif_mib_print_map);
}

const char *get_reg_name(unsigned long id)
{
	return get_symbol(id, wfx_reg_print_map);
}

static ssize_t wfx_send_pds_write(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct wfx_dev *wdev = file->private_data;
	char *buf;
	int ret;

	if (*ppos != 0) {
		dev_dbg(wdev->dev, "PDS data must be written in one transaction");
		return -EBUSY;
	}
	buf = memdup_user(user_buf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	*ppos = *ppos + count;
	ret = wfx_send_pds(wdev, buf, count);
	kfree(buf);
	if (ret < 0)
		return ret;
	return count;
}

static const struct file_operations wfx_send_pds_fops = {
	.open = simple_open,
	.write = wfx_send_pds_write,
};

static ssize_t wfx_burn_slk_key_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct wfx_dev *wdev = file->private_data;

	dev_info(wdev->dev, "this driver does not support secure link\n");
	return -EINVAL;
}

static const struct file_operations wfx_burn_slk_key_fops = {
	.open = simple_open,
	.write = wfx_burn_slk_key_write,
};

struct dbgfs_hif_msg {
	struct wfx_dev *wdev;
	struct completion complete;
	u8 reply[1024];
	int ret;
};

static ssize_t wfx_send_hif_msg_write(struct file *file, const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct dbgfs_hif_msg *context = file->private_data;
	struct wfx_dev *wdev = context->wdev;
	struct hif_msg *request;

	if (completion_done(&context->complete)) {
		dev_dbg(wdev->dev, "read previous result before start a new one\n");
		return -EBUSY;
	}
	if (count < sizeof(struct hif_msg))
		return -EINVAL;

	// wfx_cmd_send() chekc that reply buffer is wide enough, but do not
	// return precise length read. User have to know how many bytes should
	// be read. Filling reply buffer with a memory pattern may help user.
	memset(context->reply, sizeof(context->reply), 0xFF);
	request = memdup_user(user_buf, count);
	if (IS_ERR(request))
		return PTR_ERR(request);
	if (request->len != count) {
		kfree(request);
		return -EINVAL;
	}
	context->ret = wfx_cmd_send(wdev, request, context->reply, sizeof(context->reply), false);

	kfree(request);
	complete(&context->complete);
	return count;
}

static ssize_t wfx_send_hif_msg_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct dbgfs_hif_msg *context = file->private_data;
	int ret;

	if (count > sizeof(context->reply))
		return -EINVAL;
	ret = wait_for_completion_interruptible(&context->complete);
	if (ret)
		return ret;
	if (context->ret < 0)
		return context->ret;
	// Be carefull, write() is waiting for a full message while read()
	// only return a payload
	ret = copy_to_user(user_buf, context->reply, count);
	if (ret)
		return ret;

	return count;
}

static int wfx_send_hif_msg_open(struct inode *inode, struct file *file)
{
	struct dbgfs_hif_msg *context = kzalloc(sizeof(*context), GFP_KERNEL);

	if (!context)
		return -ENOMEM;
	context->wdev = inode->i_private;
	init_completion(&context->complete);
	file->private_data = context;
	return 0;
}

static int wfx_send_hif_msg_release(struct inode *inode, struct file *file)
{
	struct dbgfs_hif_msg *context = file->private_data;

	kfree(context);
	return 0;
}

static const struct file_operations wfx_send_hif_msg_fops = {
	.open = wfx_send_hif_msg_open,
	.release = wfx_send_hif_msg_release,
	.write = wfx_send_hif_msg_write,
	.read = wfx_send_hif_msg_read,
};

int wfx_debug_init(struct wfx_dev *wdev)
{
	struct dentry *d;

	d = debugfs_create_dir("wfx", wdev->hw->wiphy->debugfsdir);
	debugfs_create_file("send_pds", 0200, d, wdev, &wfx_send_pds_fops);
	debugfs_create_file("burn_slk_key", 0200, d, wdev, &wfx_burn_slk_key_fops);
	debugfs_create_file("send_hif_msg", 0600, d, wdev, &wfx_send_hif_msg_fops);

	return 0;
}
