// SPDX-License-Identifier: GPL-2.0+
// De logs for the ChromeOS EC
//
// Copyright (C) 2015 Google, Inc.

#include <linux/circ_buf.h>
#include <linux/defs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#define DRV_NAME "cros-ec-defs"

#define LOG_SHIFT		14
#define LOG_SIZE		(1 << LOG_SHIFT)
#define LOG_POLL_SEC		10

#define CIRC_ADD(idx, size, value)	(((idx) + (value)) & ((size) - 1))

/* struct cros_ec_defs - ChromeOS EC deging information
 *
 * @ec: EC device this defs information belongs to
 * @dir: dentry for defs files
 * @log_buffer: circular buffer for console log information
 * @read_msg: preallocated EC command and buffer to read console log
 * @log_mutex: mutex to protect circular buffer
 * @log_wq: waitqueue for log readers
 * @log_poll_work: recurring task to poll EC for new console log data
 * @panicinfo_blob: panicinfo defs blob
 */
struct cros_ec_defs {
	struct cros_ec_dev *ec;
	struct dentry *dir;
	/* EC log */
	struct circ_buf log_buffer;
	struct cros_ec_command *read_msg;
	struct mutex log_mutex;
	wait_queue_head_t log_wq;
	struct delayed_work log_poll_work;
	/* EC panicinfo */
	struct defs_blob_wrapper panicinfo_blob;
};

/*
 * We need to make sure that the EC log buffer on the UART is large enough,
 * so that it is unlikely enough to overlow within LOG_POLL_SEC.
 */
static void cros_ec_console_log_work(struct work_struct *__work)
{
	struct cros_ec_defs *de_info =
		container_of(to_delayed_work(__work),
			     struct cros_ec_defs,
			     log_poll_work);
	struct cros_ec_dev *ec = de_info->ec;
	struct circ_buf *cb = &de_info->log_buffer;
	struct cros_ec_command snapshot_msg = {
		.command = EC_CMD_CONSOLE_SNAPSHOT + ec->cmd_offset,
	};

	struct ec_params_console_read_v1 *read_params =
		(struct ec_params_console_read_v1 *)de_info->read_msg->data;
	uint8_t *ec_buffer = (uint8_t *)de_info->read_msg->data;
	int idx;
	int buf_space;
	int ret;

	ret = cros_ec_cmd_xfer(ec->ec_dev, &snapshot_msg);
	if (ret < 0) {
		dev_err(ec->dev, "EC communication failed\n");
		goto resched;
	}
	if (snapshot_msg.result != EC_RES_SUCCESS) {
		dev_err(ec->dev, "EC failed to snapshot the console log\n");
		goto resched;
	}

	/* Loop until we have read everything, or there's an error. */
	mutex_lock(&de_info->log_mutex);
	buf_space = CIRC_SPACE(cb->head, cb->tail, LOG_SIZE);

	while (1) {
		if (!buf_space) {
			dev_info_once(ec->dev,
				      "Some logs may have been dropped...\n");
			break;
		}

		memset(read_params, '\0', sizeof(*read_params));
		read_params->subcmd = CONSOLE_READ_RECENT;
		ret = cros_ec_cmd_xfer(ec->ec_dev, de_info->read_msg);
		if (ret < 0) {
			dev_err(ec->dev, "EC communication failed\n");
			break;
		}
		if (de_info->read_msg->result != EC_RES_SUCCESS) {
			dev_err(ec->dev,
				"EC failed to read the console log\n");
			break;
		}

		/* If the buffer is empty, we're done here. */
		if (ret == 0 || ec_buffer[0] == '\0')
			break;

		idx = 0;
		while (idx < ret && ec_buffer[idx] != '\0' && buf_space > 0) {
			cb->buf[cb->head] = ec_buffer[idx];
			cb->head = CIRC_ADD(cb->head, LOG_SIZE, 1);
			idx++;
			buf_space--;
		}

		wake_up(&de_info->log_wq);
	}

	mutex_unlock(&de_info->log_mutex);

resched:
	schedule_delayed_work(&de_info->log_poll_work,
			      msecs_to_jiffies(LOG_POLL_SEC * 1000));
}

static int cros_ec_console_log_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return nonseekable_open(inode, file);
}

static ssize_t cros_ec_console_log_read(struct file *file, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct cros_ec_defs *de_info = file->private_data;
	struct circ_buf *cb = &de_info->log_buffer;
	ssize_t ret;

	mutex_lock(&de_info->log_mutex);

	while (!CIRC_CNT(cb->head, cb->tail, LOG_SIZE)) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto error;
		}

		mutex_unlock(&de_info->log_mutex);

		ret = wait_event_interruptible(de_info->log_wq,
					CIRC_CNT(cb->head, cb->tail, LOG_SIZE));
		if (ret < 0)
			return ret;

		mutex_lock(&de_info->log_mutex);
	}

	/* Only copy until the end of the circular buffer, and let userspace
	 * retry to get the rest of the data.
	 */
	ret = min_t(size_t, CIRC_CNT_TO_END(cb->head, cb->tail, LOG_SIZE),
		    count);

	if (copy_to_user(buf, cb->buf + cb->tail, ret)) {
		ret = -EFAULT;
		goto error;
	}

	cb->tail = CIRC_ADD(cb->tail, LOG_SIZE, ret);

error:
	mutex_unlock(&de_info->log_mutex);
	return ret;
}

static __poll_t cros_ec_console_log_poll(struct file *file,
					     poll_table *wait)
{
	struct cros_ec_defs *de_info = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &de_info->log_wq, wait);

	mutex_lock(&de_info->log_mutex);
	if (CIRC_CNT(de_info->log_buffer.head,
		     de_info->log_buffer.tail,
		     LOG_SIZE))
		mask |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&de_info->log_mutex);

	return mask;
}

static int cros_ec_console_log_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t cros_ec_pdinfo_read(struct file *file,
				   char __user *user_buf,
				   size_t count,
				   loff_t *ppos)
{
	char read_buf[EC_USB_PD_MAX_PORTS * 40], *p = read_buf;
	struct cros_ec_defs *de_info = file->private_data;
	struct cros_ec_device *ec_dev = de_info->ec->ec_dev;
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_response_usb_pd_control_v1 resp;
			struct ec_params_usb_pd_control params;
		};
	} __packed ec_buf;
	struct cros_ec_command *msg;
	struct ec_response_usb_pd_control_v1 *resp;
	struct ec_params_usb_pd_control *params;
	int i;

	msg = &ec_buf.msg;
	params = (struct ec_params_usb_pd_control *)msg->data;
	resp = (struct ec_response_usb_pd_control_v1 *)msg->data;

	msg->command = EC_CMD_USB_PD_CONTROL;
	msg->version = 1;
	msg->insize = sizeof(*resp);
	msg->outsize = sizeof(*params);

	/*
	 * Read status from all PD ports until failure, typically caused
	 * by attempting to read status on a port that doesn't exist.
	 */
	for (i = 0; i < EC_USB_PD_MAX_PORTS; ++i) {
		params->port = i;
		params->role = 0;
		params->mux = 0;
		params->swap = 0;

		if (cros_ec_cmd_xfer_status(ec_dev, msg) < 0)
			break;

		p += scnprintf(p, sizeof(read_buf) + read_buf - p,
			       "p%d: %s en:%.2x role:%.2x pol:%.2x\n", i,
			       resp->state, resp->enabled, resp->role,
			       resp->polarity);
	}

	return simple_read_from_buffer(user_buf, count, ppos,
				       read_buf, p - read_buf);
}

const struct file_operations cros_ec_console_log_fops = {
	.owner = THIS_MODULE,
	.open = cros_ec_console_log_open,
	.read = cros_ec_console_log_read,
	.llseek = no_llseek,
	.poll = cros_ec_console_log_poll,
	.release = cros_ec_console_log_release,
};

const struct file_operations cros_ec_pdinfo_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = cros_ec_pdinfo_read,
	.llseek = default_llseek,
};

static int ec_read_version_supported(struct cros_ec_dev *ec)
{
	struct ec_params_get_cmd_versions_v1 *params;
	struct ec_response_get_cmd_versions *response;
	int ret;

	struct cros_ec_command *msg;

	msg = kzalloc(sizeof(*msg) + max(sizeof(*params), sizeof(*response)),
		GFP_KERNEL);
	if (!msg)
		return 0;

	msg->command = EC_CMD_GET_CMD_VERSIONS + ec->cmd_offset;
	msg->outsize = sizeof(*params);
	msg->insize = sizeof(*response);

	params = (struct ec_params_get_cmd_versions_v1 *)msg->data;
	params->cmd = EC_CMD_CONSOLE_READ;
	response = (struct ec_response_get_cmd_versions *)msg->data;

	ret = cros_ec_cmd_xfer(ec->ec_dev, msg) >= 0 &&
		msg->result == EC_RES_SUCCESS &&
		(response->version_mask & EC_VER_MASK(1));

	kfree(msg);

	return ret;
}

static int cros_ec_create_console_log(struct cros_ec_defs *de_info)
{
	struct cros_ec_dev *ec = de_info->ec;
	char *buf;
	int read_params_size;
	int read_response_size;

	if (!ec_read_version_supported(ec)) {
		dev_warn(ec->dev,
			"device does not support reading the console log\n");
		return 0;
	}

	buf = devm_kzalloc(ec->dev, LOG_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	read_params_size = sizeof(struct ec_params_console_read_v1);
	read_response_size = ec->ec_dev->max_response;
	de_info->read_msg = devm_kzalloc(ec->dev,
		sizeof(*de_info->read_msg) +
			max(read_params_size, read_response_size), GFP_KERNEL);
	if (!de_info->read_msg)
		return -ENOMEM;

	de_info->read_msg->version = 1;
	de_info->read_msg->command = EC_CMD_CONSOLE_READ + ec->cmd_offset;
	de_info->read_msg->outsize = read_params_size;
	de_info->read_msg->insize = read_response_size;

	de_info->log_buffer.buf = buf;
	de_info->log_buffer.head = 0;
	de_info->log_buffer.tail = 0;

	mutex_init(&de_info->log_mutex);
	init_waitqueue_head(&de_info->log_wq);

	if (!defs_create_file("console_log",
				 S_IFREG | 0444,
				 de_info->dir,
				 de_info,
				 &cros_ec_console_log_fops))
		return -ENOMEM;

	INIT_DELAYED_WORK(&de_info->log_poll_work,
			  cros_ec_console_log_work);
	schedule_delayed_work(&de_info->log_poll_work, 0);

	return 0;
}

static void cros_ec_cleanup_console_log(struct cros_ec_defs *de_info)
{
	if (de_info->log_buffer.buf) {
		cancel_delayed_work_sync(&de_info->log_poll_work);
		mutex_destroy(&de_info->log_mutex);
	}
}

static int cros_ec_create_panicinfo(struct cros_ec_defs *de_info)
{
	struct cros_ec_device *ec_dev = de_info->ec->ec_dev;
	int ret;
	struct cros_ec_command *msg;
	int insize;

	insize = ec_dev->max_response;

	msg = devm_kzalloc(de_info->ec->dev,
			sizeof(*msg) + insize, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_GET_PANIC_INFO;
	msg->insize = insize;

	ret = cros_ec_cmd_xfer(ec_dev, msg);
	if (ret < 0) {
		dev_warn(de_info->ec->dev, "Cannot read panicinfo.\n");
		ret = 0;
		goto free;
	}

	/* No panic data */
	if (ret == 0)
		goto free;

	de_info->panicinfo_blob.data = msg->data;
	de_info->panicinfo_blob.size = ret;

	if (!defs_create_blob("panicinfo",
				 S_IFREG | 0444,
				 de_info->dir,
				 &de_info->panicinfo_blob)) {
		ret = -ENOMEM;
		goto free;
	}

	return 0;

free:
	devm_kfree(de_info->ec->dev, msg);
	return ret;
}

static int cros_ec_create_pdinfo(struct cros_ec_defs *de_info)
{
	if (!defs_create_file("pdinfo", 0444, de_info->dir, de_info,
				 &cros_ec_pdinfo_fops))
		return -ENOMEM;

	return 0;
}

static int cros_ec_defs_probe(struct platform_device *pd)
{
	struct cros_ec_dev *ec = dev_get_drvdata(pd->dev.parent);
	struct cros_ec_platform *ec_platform = dev_get_platdata(ec->dev);
	const char *name = ec_platform->ec_name;
	struct cros_ec_defs *de_info;
	int ret;

	de_info = devm_kzalloc(ec->dev, sizeof(*de_info), GFP_KERNEL);
	if (!de_info)
		return -ENOMEM;

	de_info->ec = ec;
	de_info->dir = defs_create_dir(name, NULL);
	if (!de_info->dir)
		return -ENOMEM;

	ret = cros_ec_create_panicinfo(de_info);
	if (ret)
		goto remove_defs;

	ret = cros_ec_create_console_log(de_info);
	if (ret)
		goto remove_defs;

	ret = cros_ec_create_pdinfo(de_info);
	if (ret)
		goto remove_log;

	ec->de_info = de_info;

	dev_set_drvdata(&pd->dev, ec);

	return 0;

remove_log:
	cros_ec_cleanup_console_log(de_info);
remove_defs:
	defs_remove_recursive(de_info->dir);
	return ret;
}

static int cros_ec_defs_remove(struct platform_device *pd)
{
	struct cros_ec_dev *ec = dev_get_drvdata(pd->dev.parent);

	defs_remove_recursive(ec->de_info->dir);
	cros_ec_cleanup_console_log(ec->de_info);

	return 0;
}

static int __maybe_unused cros_ec_defs_suspend(struct device *dev)
{
	struct cros_ec_dev *ec = dev_get_drvdata(dev);

	if (ec->de_info->log_buffer.buf)
		cancel_delayed_work_sync(&ec->de_info->log_poll_work);

	return 0;
}

static int __maybe_unused cros_ec_defs_resume(struct device *dev)
{
	struct cros_ec_dev *ec = dev_get_drvdata(dev);

	if (ec->de_info->log_buffer.buf)
		schedule_delayed_work(&ec->de_info->log_poll_work, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(cros_ec_defs_pm_ops,
			 cros_ec_defs_suspend, cros_ec_defs_resume);

static struct platform_driver cros_ec_defs_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_ec_defs_pm_ops,
	},
	.probe = cros_ec_defs_probe,
	.remove = cros_ec_defs_remove,
};

module_platform_driver(cros_ec_defs_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("De logs for ChromeOS EC");
MODULE_ALIAS("platform:" DRV_NAME);
