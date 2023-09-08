// SPDX-License-Identifier: GPL-2.0+
// Expose the Chromebook Pixel lightbar to userspace
//
// Copyright (C) 2014 Google, Inc.

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DRV_NAME "cros-ec-lightbar"

/* Rate-limit the lightbar interface to prevent DoS. */
static unsigned long lb_interval_jiffies = 50 * HZ / 1000;

/*
 * Whether or not we have given userspace control of the lightbar.
 * If this is true, we won't do anything during suspend/resume.
 */
static bool userspace_control;

static ssize_t interval_msec_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned long msec = lb_interval_jiffies * 1000 / HZ;

	return sysfs_emit(buf, "%lu\n", msec);
}

static ssize_t interval_msec_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned long msec;

	if (kstrtoul(buf, 0, &msec))
		return -EINVAL;

	lb_interval_jiffies = msec * HZ / 1000;

	return count;
}

static DEFINE_MUTEX(lb_mutex);
/* Return 0 if able to throttle correctly, error otherwise */
static int lb_throttle(void)
{
	static unsigned long last_access;
	unsigned long now, next_timeslot;
	long delay;
	int ret = 0;

	mutex_lock(&lb_mutex);

	now = jiffies;
	next_timeslot = last_access + lb_interval_jiffies;

	if (time_before(now, next_timeslot)) {
		delay = (long)(next_timeslot) - (long)now;
		set_current_state(TASK_INTERRUPTIBLE);
		if (schedule_timeout(delay) > 0) {
			/* interrupted - just abort */
			ret = -EINTR;
			goto out;
		}
		now = jiffies;
	}

	last_access = now;
out:
	mutex_unlock(&lb_mutex);

	return ret;
}

static struct cros_ec_command *alloc_lightbar_cmd_msg(struct cros_ec_dev *ec)
{
	struct cros_ec_command *msg;
	int len;

	len = max(sizeof(struct ec_params_lightbar),
		  sizeof(struct ec_response_lightbar));

	msg = kmalloc(sizeof(*msg) + len, GFP_KERNEL);
	if (!msg)
		return NULL;

	msg->version = 0;
	msg->command = EC_CMD_LIGHTBAR_CMD + ec->cmd_offset;
	msg->outsize = sizeof(struct ec_params_lightbar);
	msg->insize = sizeof(struct ec_response_lightbar);

	return msg;
}

static int get_lightbar_version(struct cros_ec_dev *ec,
				uint32_t *ver_ptr, uint32_t *flg_ptr)
{
	struct ec_params_lightbar *param;
	struct ec_response_lightbar *resp;
	struct cros_ec_command *msg;
	int ret;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return 0;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_VERSION;
	msg->outsize = sizeof(param->cmd);
	msg->result = sizeof(resp->version);
	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0 && ret != -EINVAL) {
		ret = 0;
		goto exit;
	}

	switch (msg->result) {
	case EC_RES_INVALID_PARAM:
		/* Pixel had no version command. */
		if (ver_ptr)
			*ver_ptr = 0;
		if (flg_ptr)
			*flg_ptr = 0;
		ret = 1;
		goto exit;

	case EC_RES_SUCCESS:
		resp = (struct ec_response_lightbar *)msg->data;

		/* Future devices w/lightbars should implement this command */
		if (ver_ptr)
			*ver_ptr = resp->version.num;
		if (flg_ptr)
			*flg_ptr = resp->version.flags;
		ret = 1;
		goto exit;
	}

	/* Anything else (ie, EC_RES_INVALID_COMMAND) - no lightbar */
	ret = 0;
exit:
	kfree(msg);
	return ret;
}

static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	uint32_t version = 0, flags = 0;
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);
	int ret;

	ret = lb_throttle();
	if (ret)
		return ret;

	/* This should always succeed, because we check during init. */
	if (!get_lightbar_version(ec, &version, &flags))
		return -EIO;

	return sysfs_emit(buf, "%d %d\n", version, flags);
}

static ssize_t brightness_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	int ret;
	unsigned int val;
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_SET_BRIGHTNESS;
	param->set_brightness.num = val;
	ret = lb_throttle();
	if (ret)
		goto exit;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;

	ret = count;
exit:
	kfree(msg);
	return ret;
}


/*
 * We expect numbers, and we'll keep reading until we find them, skipping over
 * any whitespace (sysfs guarantees that the input is null-terminated). Every
 * four numbers are sent to the lightbar as <LED,R,G,B>. We fail at the first
 * parsing error, if we don't parse any numbers, or if we have numbers left
 * over.
 */
static ssize_t led_rgb_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);
	unsigned int val[4];
	int ret, i = 0, j = 0, ok = 0;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	do {
		/* Skip any whitespace */
		while (*buf && isspace(*buf))
			buf++;

		if (!*buf)
			break;

		ret = sscanf(buf, "%i", &val[i++]);
		if (ret == 0)
			goto exit;

		if (i == 4) {
			param = (struct ec_params_lightbar *)msg->data;
			param->cmd = LIGHTBAR_CMD_SET_RGB;
			param->set_rgb.led = val[0];
			param->set_rgb.red = val[1];
			param->set_rgb.green = val[2];
			param->set_rgb.blue = val[3];
			/*
			 * Throttle only the first of every four transactions,
			 * so that the user can update all four LEDs at once.
			 */
			if ((j++ % 4) == 0) {
				ret = lb_throttle();
				if (ret)
					goto exit;
			}

			ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
			if (ret < 0)
				goto exit;

			i = 0;
			ok = 1;
		}

		/* Skip over the number we just read */
		while (*buf && !isspace(*buf))
			buf++;

	} while (*buf);

exit:
	kfree(msg);
	return (ok && i == 0) ? count : -EINVAL;
}

static char const *seqname[] = {
	"ERROR", "S5", "S3", "S0", "S5S3", "S3S0",
	"S0S3", "S3S5", "STOP", "RUN", "KONAMI",
	"TAP", "PROGRAM",
};

static ssize_t sequence_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ec_params_lightbar *param;
	struct ec_response_lightbar *resp;
	struct cros_ec_command *msg;
	int ret;
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_GET_SEQ;
	ret = lb_throttle();
	if (ret)
		goto exit;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0) {
		ret = sysfs_emit(buf, "XFER / EC ERROR %d / %d\n", ret, msg->result);
		goto exit;
	}

	resp = (struct ec_response_lightbar *)msg->data;
	if (resp->get_seq.num >= ARRAY_SIZE(seqname))
		ret = sysfs_emit(buf, "%d\n", resp->get_seq.num);
	else
		ret = sysfs_emit(buf, "%s\n", seqname[resp->get_seq.num]);

exit:
	kfree(msg);
	return ret;
}

static int lb_send_empty_cmd(struct cros_ec_dev *ec, uint8_t cmd)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	int ret;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = cmd;

	ret = lb_throttle();
	if (ret)
		goto error;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0)
		goto error;

	ret = 0;
error:
	kfree(msg);

	return ret;
}

static int lb_manual_suspend_ctrl(struct cros_ec_dev *ec, uint8_t enable)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	int ret;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;

	param->cmd = LIGHTBAR_CMD_MANUAL_SUSPEND_CTRL;
	param->manual_suspend_ctrl.enable = enable;

	ret = lb_throttle();
	if (ret)
		goto error;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0)
		goto error;

	ret = 0;
error:
	kfree(msg);

	return ret;
}

static ssize_t sequence_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	unsigned int num;
	int ret, len;
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);

	for (len = 0; len < count; len++)
		if (!isalnum(buf[len]))
			break;

	for (num = 0; num < ARRAY_SIZE(seqname); num++)
		if (!strncasecmp(seqname[num], buf, len))
			break;

	if (num >= ARRAY_SIZE(seqname)) {
		ret = kstrtouint(buf, 0, &num);
		if (ret)
			return ret;
	}

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_SEQ;
	param->seq.num = num;
	ret = lb_throttle();
	if (ret)
		goto exit;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;

	ret = count;
exit:
	kfree(msg);
	return ret;
}

static ssize_t program_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int extra_bytes, max_size, ret;
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	struct cros_ec_dev *ec = to_cros_ec_dev(dev);

	/*
	 * We might need to reject the program for size reasons. The EC
	 * enforces a maximum program size, but we also don't want to try
	 * and send a program that is too big for the protocol. In order
	 * to ensure the latter, we also need to ensure we have extra bytes
	 * to represent the rest of the packet.
	 */
	extra_bytes = sizeof(*param) - sizeof(param->set_program.data);
	max_size = min(EC_LB_PROG_LEN, ec->ec_dev->max_request - extra_bytes);
	if (count > max_size) {
		dev_err(dev, "Program is %u bytes, too long to send (max: %u)",
			(unsigned int)count, max_size);

		return -EINVAL;
	}

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	ret = lb_throttle();
	if (ret)
		goto exit;

	dev_info(dev, "Copying %zu byte program to EC", count);

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_SET_PROGRAM;

	param->set_program.size = count;
	memcpy(param->set_program.data, buf, count);

	/*
	 * We need to set the message size manually or else it will use
	 * EC_LB_PROG_LEN. This might be too long, and the program
	 * is unlikely to use all of the space.
	 */
	msg->outsize = count + extra_bytes;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;

	ret = count;
exit:
	kfree(msg);

	return ret;
}

static ssize_t userspace_control_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sysfs_emit(buf, "%d\n", userspace_control);
}

static ssize_t userspace_control_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	userspace_control = enable;

	return count;
}

/* Module initialization */

static DEVICE_ATTR_RW(interval_msec);
static DEVICE_ATTR_RO(version);
static DEVICE_ATTR_WO(brightness);
static DEVICE_ATTR_WO(led_rgb);
static DEVICE_ATTR_RW(sequence);
static DEVICE_ATTR_WO(program);
static DEVICE_ATTR_RW(userspace_control);

static struct attribute *__lb_cmds_attrs[] = {
	&dev_attr_interval_msec.attr,
	&dev_attr_version.attr,
	&dev_attr_brightness.attr,
	&dev_attr_led_rgb.attr,
	&dev_attr_sequence.attr,
	&dev_attr_program.attr,
	&dev_attr_userspace_control.attr,
	NULL,
};

static const struct attribute_group cros_ec_lightbar_attr_group = {
	.name = "lightbar",
	.attrs = __lb_cmds_attrs,
};

static int cros_ec_lightbar_probe(struct platform_device *pd)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pd->dev.parent);
	struct cros_ec_platform *pdata = dev_get_platdata(ec_dev->dev);
	struct device *dev = &pd->dev;
	int ret;

	/*
	 * Only instantiate the lightbar if the EC name is 'cros_ec'. Other EC
	 * devices like 'cros_pd' doesn't have a lightbar.
	 */
	if (strcmp(pdata->ec_name, CROS_EC_DEV_NAME) != 0)
		return -ENODEV;

	/*
	 * Ask then for the lightbar version, if it's 0 then the 'cros_ec'
	 * doesn't have a lightbar.
	 */
	if (!get_lightbar_version(ec_dev, NULL, NULL))
		return -ENODEV;

	/* Take control of the lightbar from the EC. */
	lb_manual_suspend_ctrl(ec_dev, 1);

	ret = sysfs_create_group(&ec_dev->class_dev.kobj,
				 &cros_ec_lightbar_attr_group);
	if (ret < 0)
		dev_err(dev, "failed to create %s attributes. err=%d\n",
			cros_ec_lightbar_attr_group.name, ret);

	return ret;
}

static int cros_ec_lightbar_remove(struct platform_device *pd)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pd->dev.parent);

	sysfs_remove_group(&ec_dev->class_dev.kobj,
			   &cros_ec_lightbar_attr_group);

	/* Let the EC take over the lightbar again. */
	lb_manual_suspend_ctrl(ec_dev, 0);

	return 0;
}

static int __maybe_unused cros_ec_lightbar_resume(struct device *dev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);

	if (userspace_control)
		return 0;

	return lb_send_empty_cmd(ec_dev, LIGHTBAR_CMD_RESUME);
}

static int __maybe_unused cros_ec_lightbar_suspend(struct device *dev)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);

	if (userspace_control)
		return 0;

	return lb_send_empty_cmd(ec_dev, LIGHTBAR_CMD_SUSPEND);
}

static SIMPLE_DEV_PM_OPS(cros_ec_lightbar_pm_ops,
			 cros_ec_lightbar_suspend, cros_ec_lightbar_resume);

static struct platform_driver cros_ec_lightbar_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_ec_lightbar_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = cros_ec_lightbar_probe,
	.remove = cros_ec_lightbar_remove,
};

module_platform_driver(cros_ec_lightbar_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Expose the Chromebook Pixel's lightbar to userspace");
MODULE_ALIAS("platform:" DRV_NAME);
