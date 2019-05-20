/*
 *	w1_ds2408.c - w1 family 29 (DS2408) driver
 *
 * Copyright (c) 2010 Jean-Francois Dagenais <dagenaisj@sonatest.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/w1.h>

#define W1_FAMILY_DS2408	0x29

#define W1_F29_RETRIES		3

#define W1_F29_REG_LOGIG_STATE             0x88 /* R */
#define W1_F29_REG_OUTPUT_LATCH_STATE      0x89 /* R */
#define W1_F29_REG_ACTIVITY_LATCH_STATE    0x8A /* R */
#define W1_F29_REG_COND_SEARCH_SELECT_MASK 0x8B /* RW */
#define W1_F29_REG_COND_SEARCH_POL_SELECT  0x8C /* RW */
#define W1_F29_REG_CONTROL_AND_STATUS      0x8D /* RW */

#define W1_F29_FUNC_READ_PIO_REGS          0xF0
#define W1_F29_FUNC_CHANN_ACCESS_READ      0xF5
#define W1_F29_FUNC_CHANN_ACCESS_WRITE     0x5A
/* also used to write the control/status reg (0x8D): */
#define W1_F29_FUNC_WRITE_COND_SEARCH_REG  0xCC
#define W1_F29_FUNC_RESET_ACTIVITY_LATCHES 0xC3

#define W1_F29_SUCCESS_CONFIRM_BYTE        0xAA

static int _read_reg(struct w1_slave *sl, u8 address, unsigned char* buf)
{
	u8 wrbuf[3];
	dev_dbg(&sl->dev,
			"Reading with slave: %p, reg addr: %0#4x, buff addr: %p",
			sl, (unsigned int)address, buf);

	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex locked");

	if (w1_reset_select_slave(sl)) {
		mutex_unlock(&sl->master->bus_mutex);
		return -EIO;
	}

	wrbuf[0] = W1_F29_FUNC_READ_PIO_REGS;
	wrbuf[1] = address;
	wrbuf[2] = 0;
	w1_write_block(sl->master, wrbuf, 3);
	*buf = w1_read_8(sl->master);

	mutex_unlock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex unlocked");
	return 1;
}

static ssize_t state_read(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *bin_attr, char *buf, loff_t off,
			  size_t count)
{
	dev_dbg(&kobj_to_w1_slave(kobj)->dev,
		"Reading %s kobj: %p, off: %0#10x, count: %zu, buff addr: %p",
		bin_attr->attr.name, kobj, (unsigned int)off, count, buf);
	if (count != 1 || off != 0)
		return -EFAULT;
	return _read_reg(kobj_to_w1_slave(kobj), W1_F29_REG_LOGIG_STATE, buf);
}

static ssize_t output_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr, char *buf,
			   loff_t off, size_t count)
{
	dev_dbg(&kobj_to_w1_slave(kobj)->dev,
		"Reading %s kobj: %p, off: %0#10x, count: %zu, buff addr: %p",
		bin_attr->attr.name, kobj, (unsigned int)off, count, buf);
	if (count != 1 || off != 0)
		return -EFAULT;
	return _read_reg(kobj_to_w1_slave(kobj),
					 W1_F29_REG_OUTPUT_LATCH_STATE, buf);
}

static ssize_t activity_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	dev_dbg(&kobj_to_w1_slave(kobj)->dev,
		"Reading %s kobj: %p, off: %0#10x, count: %zu, buff addr: %p",
		bin_attr->attr.name, kobj, (unsigned int)off, count, buf);
	if (count != 1 || off != 0)
		return -EFAULT;
	return _read_reg(kobj_to_w1_slave(kobj),
					 W1_F29_REG_ACTIVITY_LATCH_STATE, buf);
}

static ssize_t cond_search_mask_read(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *bin_attr, char *buf,
				     loff_t off, size_t count)
{
	dev_dbg(&kobj_to_w1_slave(kobj)->dev,
		"Reading %s kobj: %p, off: %0#10x, count: %zu, buff addr: %p",
		bin_attr->attr.name, kobj, (unsigned int)off, count, buf);
	if (count != 1 || off != 0)
		return -EFAULT;
	return _read_reg(kobj_to_w1_slave(kobj),
		W1_F29_REG_COND_SEARCH_SELECT_MASK, buf);
}

static ssize_t cond_search_polarity_read(struct file *filp,
					 struct kobject *kobj,
					 struct bin_attribute *bin_attr,
					 char *buf, loff_t off, size_t count)
{
	if (count != 1 || off != 0)
		return -EFAULT;
	return _read_reg(kobj_to_w1_slave(kobj),
		W1_F29_REG_COND_SEARCH_POL_SELECT, buf);
}

static ssize_t status_control_read(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr, char *buf,
				   loff_t off, size_t count)
{
	if (count != 1 || off != 0)
		return -EFAULT;
	return _read_reg(kobj_to_w1_slave(kobj),
		W1_F29_REG_CONTROL_AND_STATUS, buf);
}

#ifdef fCONFIG_W1_SLAVE_DS2408_READBACK
static bool optional_read_back_valid(struct w1_slave *sl, u8 expected)
{
	u8 w1_buf[3];

	if (w1_reset_resume_command(sl->master))
		return false;

	w1_buf[0] = W1_F29_FUNC_READ_PIO_REGS;
	w1_buf[1] = W1_F29_REG_OUTPUT_LATCH_STATE;
	w1_buf[2] = 0;

	w1_write_block(sl->master, w1_buf, 3);

	return (w1_read_8(sl->master) == expected);
}
#else
static bool optional_read_back_valid(struct w1_slave *sl, u8 expected)
{
	return true;
}
#endif

static ssize_t output_write(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 w1_buf[3];
	unsigned int retries = W1_F29_RETRIES;
	ssize_t bytes_written = -EIO;

	if (count != 1 || off != 0)
		return -EFAULT;

	dev_dbg(&sl->dev, "locking mutex for write_output");
	mutex_lock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex locked");

	if (w1_reset_select_slave(sl))
		goto out;

	do {
		w1_buf[0] = W1_F29_FUNC_CHANN_ACCESS_WRITE;
		w1_buf[1] = *buf;
		w1_buf[2] = ~(*buf);

		w1_write_block(sl->master, w1_buf, 3);

		if (w1_read_8(sl->master) == W1_F29_SUCCESS_CONFIRM_BYTE &&
		    optional_read_back_valid(sl, *buf)) {
			bytes_written = 1;
			goto out;
		}

		if (w1_reset_resume_command(sl->master))
			goto out; /* unrecoverable error */
		/* try again, the slave is ready for a command */
	} while (--retries);

out:
	mutex_unlock(&sl->master->bus_mutex);

	dev_dbg(&sl->dev, "%s, mutex unlocked retries:%d\n",
		(bytes_written > 0) ? "succeeded" : "error", retries);

	return bytes_written;
}


/**
 * Writing to the activity file resets the activity latches.
 */
static ssize_t activity_write(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr, char *buf,
			      loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	unsigned int retries = W1_F29_RETRIES;

	if (count != 1 || off != 0)
		return -EFAULT;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl))
		goto error;

	while (retries--) {
		w1_write_8(sl->master, W1_F29_FUNC_RESET_ACTIVITY_LATCHES);
		if (w1_read_8(sl->master) == W1_F29_SUCCESS_CONFIRM_BYTE) {
			mutex_unlock(&sl->master->bus_mutex);
			return 1;
		}
		if (w1_reset_resume_command(sl->master))
			goto error;
	}

error:
	mutex_unlock(&sl->master->bus_mutex);
	return -EIO;
}

static ssize_t status_control_write(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *bin_attr, char *buf,
				    loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 w1_buf[4];
	unsigned int retries = W1_F29_RETRIES;

	if (count != 1 || off != 0)
		return -EFAULT;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl))
		goto error;

	while (retries--) {
		w1_buf[0] = W1_F29_FUNC_WRITE_COND_SEARCH_REG;
		w1_buf[1] = W1_F29_REG_CONTROL_AND_STATUS;
		w1_buf[2] = 0;
		w1_buf[3] = *buf;

		w1_write_block(sl->master, w1_buf, 4);
		if (w1_reset_resume_command(sl->master))
			goto error;

		w1_buf[0] = W1_F29_FUNC_READ_PIO_REGS;
		w1_buf[1] = W1_F29_REG_CONTROL_AND_STATUS;
		w1_buf[2] = 0;

		w1_write_block(sl->master, w1_buf, 3);
		if (w1_read_8(sl->master) == *buf) {
			/* success! */
			mutex_unlock(&sl->master->bus_mutex);
			return 1;
		}
	}
error:
	mutex_unlock(&sl->master->bus_mutex);

	return -EIO;
}

/*
 * This is a special sequence we must do to ensure the P0 output is not stuck
 * in test mode. This is described in rev 2 of the ds2408's datasheet
 * (http://datasheets.maximintegrated.com/en/ds/DS2408.pdf) under
 * "APPLICATION INFORMATION/Power-up timing".
 */
static int w1_f29_disable_test_mode(struct w1_slave *sl)
{
	int res;
	u8 magic[10] = {0x96, };
	u64 rn = le64_to_cpu(*((u64*)&sl->reg_num));

	memcpy(&magic[1], &rn, 8);
	magic[9] = 0x3C;

	mutex_lock(&sl->master->bus_mutex);

	res = w1_reset_bus(sl->master);
	if (res)
		goto out;
	w1_write_block(sl->master, magic, ARRAY_SIZE(magic));

	res = w1_reset_bus(sl->master);
out:
	mutex_unlock(&sl->master->bus_mutex);
	return res;
}

static BIN_ATTR_RO(state, 1);
static BIN_ATTR_RW(output, 1);
static BIN_ATTR_RW(activity, 1);
static BIN_ATTR_RO(cond_search_mask, 1);
static BIN_ATTR_RO(cond_search_polarity, 1);
static BIN_ATTR_RW(status_control, 1);

static struct bin_attribute *w1_f29_bin_attrs[] = {
	&bin_attr_state,
	&bin_attr_output,
	&bin_attr_activity,
	&bin_attr_cond_search_mask,
	&bin_attr_cond_search_polarity,
	&bin_attr_status_control,
	NULL,
};

static const struct attribute_group w1_f29_group = {
	.bin_attrs = w1_f29_bin_attrs,
};

static const struct attribute_group *w1_f29_groups[] = {
	&w1_f29_group,
	NULL,
};

static struct w1_family_ops w1_f29_fops = {
	.add_slave      = w1_f29_disable_test_mode,
	.groups		= w1_f29_groups,
};

static struct w1_family w1_family_29 = {
	.fid = W1_FAMILY_DS2408,
	.fops = &w1_f29_fops,
};
module_w1_family(w1_family_29);

MODULE_AUTHOR("Jean-Francois Dagenais <dagenaisj@sonatest.com>");
MODULE_DESCRIPTION("w1 family 29 driver for DS2408 8 Pin IO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2408));
