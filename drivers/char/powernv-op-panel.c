// SPDX-License-Identifier: GPL-2.0-only
/*
 * OPAL Operator Panel Display Driver
 *
 * Copyright 2016, Suraj Jitindar Singh, IBM Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include <asm/opal.h>

/*
 * This driver creates a character device (/dev/op_panel) which exposes the
 * operator panel (character LCD display) on IBM Power Systems machines
 * with FSPs.
 * A character buffer written to the device will be displayed on the
 * operator panel.
 */

static DEFINE_MUTEX(oppanel_mutex);

static u32		num_lines, oppanel_size;
static oppanel_line_t	*oppanel_lines;
static char		*oppanel_data;

static loff_t oppanel_llseek(struct file *filp, loff_t offset, int whence)
{
	return fixed_size_llseek(filp, offset, whence, oppanel_size);
}

static ssize_t oppanel_read(struct file *filp, char __user *userbuf, size_t len,
			    loff_t *f_pos)
{
	return simple_read_from_buffer(userbuf, len, f_pos, oppanel_data,
			oppanel_size);
}

static int __op_panel_update_display(void)
{
	struct opal_msg msg;
	int rc, token;

	token = opal_async_get_token_interruptible();
	if (token < 0) {
		if (token != -ERESTARTSYS)
			pr_debug("Couldn't get OPAL async token [token=%d]\n",
				token);
		return token;
	}

	rc = opal_write_oppanel_async(token, oppanel_lines, num_lines);
	switch (rc) {
	case OPAL_ASYNC_COMPLETION:
		rc = opal_async_wait_response(token, &msg);
		if (rc) {
			pr_debug("Failed to wait for async response [rc=%d]\n",
				rc);
			break;
		}
		rc = opal_get_async_rc(msg);
		if (rc != OPAL_SUCCESS) {
			pr_debug("OPAL async call returned failed [rc=%d]\n",
				rc);
			break;
		}
	case OPAL_SUCCESS:
		break;
	default:
		pr_debug("OPAL write op-panel call failed [rc=%d]\n", rc);
	}

	opal_async_release_token(token);
	return rc;
}

static ssize_t oppanel_write(struct file *filp, const char __user *userbuf,
			     size_t len, loff_t *f_pos)
{
	loff_t f_pos_prev = *f_pos;
	ssize_t ret;
	int rc;

	if (!*f_pos)
		memset(oppanel_data, ' ', oppanel_size);
	else if (*f_pos >= oppanel_size)
		return -EFBIG;

	ret = simple_write_to_buffer(oppanel_data, oppanel_size, f_pos, userbuf,
			len);
	if (ret > 0) {
		rc = __op_panel_update_display();
		if (rc != OPAL_SUCCESS) {
			pr_err_ratelimited("OPAL call failed to write to op panel display [rc=%d]\n",
				rc);
			*f_pos = f_pos_prev;
			return -EIO;
		}
	}
	return ret;
}

static int oppanel_open(struct inode *inode, struct file *filp)
{
	if (!mutex_trylock(&oppanel_mutex)) {
		pr_debug("Device Busy\n");
		return -EBUSY;
	}
	return 0;
}

static int oppanel_release(struct inode *inode, struct file *filp)
{
	mutex_unlock(&oppanel_mutex);
	return 0;
}

static const struct file_operations oppanel_fops = {
	.owner		= THIS_MODULE,
	.llseek		= oppanel_llseek,
	.read		= oppanel_read,
	.write		= oppanel_write,
	.open		= oppanel_open,
	.release	= oppanel_release
};

static struct miscdevice oppanel_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "op_panel",
	.fops		= &oppanel_fops
};

static int oppanel_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	u32 line_len;
	int rc, i;

	rc = of_property_read_u32(np, "#length", &line_len);
	if (rc) {
		pr_err_ratelimited("Operator panel length property not found\n");
		return rc;
	}
	rc = of_property_read_u32(np, "#lines", &num_lines);
	if (rc) {
		pr_err_ratelimited("Operator panel lines property not found\n");
		return rc;
	}
	oppanel_size = line_len * num_lines;

	pr_devel("Operator panel of size %u found with %u lines of length %u\n",
			oppanel_size, num_lines, line_len);

	oppanel_data = kcalloc(oppanel_size, sizeof(*oppanel_data), GFP_KERNEL);
	if (!oppanel_data)
		return -ENOMEM;

	oppanel_lines = kcalloc(num_lines, sizeof(oppanel_line_t), GFP_KERNEL);
	if (!oppanel_lines) {
		rc = -ENOMEM;
		goto free_oppanel_data;
	}

	memset(oppanel_data, ' ', oppanel_size);
	for (i = 0; i < num_lines; i++) {
		oppanel_lines[i].line_len = cpu_to_be64(line_len);
		oppanel_lines[i].line = cpu_to_be64(__pa(&oppanel_data[i *
						line_len]));
	}

	rc = misc_register(&oppanel_dev);
	if (rc) {
		pr_err_ratelimited("Failed to register as misc device\n");
		goto free_oppanel;
	}

	return 0;

free_oppanel:
	kfree(oppanel_lines);
free_oppanel_data:
	kfree(oppanel_data);
	return rc;
}

static int oppanel_remove(struct platform_device *pdev)
{
	misc_deregister(&oppanel_dev);
	kfree(oppanel_lines);
	kfree(oppanel_data);
	return 0;
}

static const struct of_device_id oppanel_match[] = {
	{ .compatible = "ibm,opal-oppanel" },
	{ },
};

static struct platform_driver oppanel_driver = {
	.driver	= {
		.name		= "powernv-op-panel",
		.of_match_table	= oppanel_match,
	},
	.probe	= oppanel_probe,
	.remove	= oppanel_remove,
};

module_platform_driver(oppanel_driver);

MODULE_DEVICE_TABLE(of, oppanel_match);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PowerNV Operator Panel LCD Display Driver");
MODULE_AUTHOR("Suraj Jitindar Singh <sjitindarsingh@gmail.com>");
