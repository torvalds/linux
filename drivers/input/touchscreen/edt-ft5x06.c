// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Simon Budig, <simon.budig@kernelconcepts.de>
 * Daniel Wagener <daniel.wagener@kernelconcepts.de> (M09 firmware support)
 * Lothar Waßmann <LW@KARO-electronics.de> (DT support)
 * Dario Binacchi <dario.binacchi@amarulasolutions.com> (regmap support)
 */

/*
 * This is a driver for the EDT "Polytouch" family of touch controllers
 * based on the FocalTech FT5x06 line of chips.
 *
 * Development of this driver has been sponsored by Glyn:
 *    http://www.glyn.com/Products/Displays
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/ratelimit.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/unaligned.h>

#define WORK_REGISTER_THRESHOLD		0x00
#define WORK_REGISTER_REPORT_RATE	0x08
#define WORK_REGISTER_GAIN		0x30
#define WORK_REGISTER_OFFSET		0x31
#define WORK_REGISTER_NUM_X		0x33
#define WORK_REGISTER_NUM_Y		0x34

#define PMOD_REGISTER_ACTIVE		0x00
#define PMOD_REGISTER_HIBERNATE		0x03

#define M09_REGISTER_THRESHOLD		0x80
#define M09_REGISTER_GAIN		0x92
#define M09_REGISTER_OFFSET		0x93
#define M09_REGISTER_NUM_X		0x94
#define M09_REGISTER_NUM_Y		0x95

#define M12_REGISTER_REPORT_RATE	0x88

#define EV_REGISTER_THRESHOLD		0x40
#define EV_REGISTER_GAIN		0x41
#define EV_REGISTER_OFFSET_Y		0x45
#define EV_REGISTER_OFFSET_X		0x46

#define NO_REGISTER			0xff

#define WORK_REGISTER_OPMODE		0x3c
#define FACTORY_REGISTER_OPMODE		0x01
#define PMOD_REGISTER_OPMODE		0xa5

#define TOUCH_EVENT_DOWN		0x00
#define TOUCH_EVENT_UP			0x01
#define TOUCH_EVENT_ON			0x02
#define TOUCH_EVENT_RESERVED		0x03

#define EDT_NAME_LEN			23
#define EDT_SWITCH_MODE_RETRIES		10
#define EDT_SWITCH_MODE_DELAY		5 /* msec */
#define EDT_RAW_DATA_RETRIES		100
#define EDT_RAW_DATA_DELAY		1000 /* usec */

#define EDT_DEFAULT_NUM_X		1024
#define EDT_DEFAULT_NUM_Y		1024

#define M06_REG_CMD(factory) ((factory) ? 0xf3 : 0xfc)
#define M06_REG_ADDR(factory, addr) ((factory) ? (addr) & 0x7f : (addr) & 0x3f)

enum edt_pmode {
	EDT_PMODE_NOT_SUPPORTED,
	EDT_PMODE_HIBERNATE,
	EDT_PMODE_POWEROFF,
};

enum edt_ver {
	EDT_M06,
	EDT_M09,
	EDT_M12,
	EV_FT,
	GENERIC_FT,
};

struct edt_reg_addr {
	int reg_threshold;
	int reg_report_rate;
	int reg_gain;
	int reg_offset;
	int reg_offset_x;
	int reg_offset_y;
	int reg_num_x;
	int reg_num_y;
};

struct edt_ft5x06_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct touchscreen_properties prop;
	u16 num_x;
	u16 num_y;
	struct regulator *vcc;
	struct regulator *iovcc;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *wake_gpio;

	struct regmap *regmap;

#if defined(CONFIG_DEBUG_FS)
	struct dentry *debug_dir;
	u8 *raw_buffer;
	size_t raw_bufsize;
#endif

	struct mutex mutex;
	bool factory_mode;
	enum edt_pmode suspend_mode;
	int threshold;
	int gain;
	int offset;
	int offset_x;
	int offset_y;
	int report_rate;
	int max_support_points;
	int point_len;
	u8 tdata_cmd;
	int tdata_len;
	int tdata_offset;

	char name[EDT_NAME_LEN];
	char fw_version[EDT_NAME_LEN];

	struct edt_reg_addr reg_addr;
	enum edt_ver version;
	unsigned int crc_errors;
	unsigned int header_errors;
};

struct edt_i2c_chip_data {
	int  max_support_points;
};

static const struct regmap_config edt_ft5x06_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static bool edt_ft5x06_ts_check_crc(struct edt_ft5x06_ts_data *tsdata,
				    u8 *buf, int buflen)
{
	int i;
	u8 crc = 0;

	for (i = 0; i < buflen - 1; i++)
		crc ^= buf[i];

	if (crc != buf[buflen - 1]) {
		tsdata->crc_errors++;
		dev_err_ratelimited(&tsdata->client->dev,
				    "crc error: 0x%02x expected, got 0x%02x\n",
				    crc, buf[buflen - 1]);
		return false;
	}

	return true;
}

static int edt_M06_i2c_read(void *context, const void *reg_buf, size_t reg_size,
			    void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(i2c);
	struct i2c_msg xfer[2];
	bool reg_read = false;
	u8 addr;
	u8 wlen;
	u8 wbuf[4], rbuf[3];
	int ret;

	addr = *((u8 *)reg_buf);
	wbuf[0] = addr;
	switch (addr) {
	case 0xf5:
		wlen = 3;
		wbuf[0] = 0xf5;
		wbuf[1] = 0xe;
		wbuf[2] = *((u8 *)val_buf);
		break;
	case 0xf9:
		wlen = 1;
		break;
	default:
		wlen = 2;
		reg_read = true;
		wbuf[0] = M06_REG_CMD(tsdata->factory_mode);
		wbuf[1] = M06_REG_ADDR(tsdata->factory_mode, addr);
		wbuf[1] |= tsdata->factory_mode ? 0x80 : 0x40;
	}

	xfer[0].addr  = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = wlen;
	xfer[0].buf = wbuf;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = reg_read ? 2 : val_size;
	xfer[1].buf = reg_read ? rbuf : val_buf;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret != 2) {
		if (ret < 0)
			return ret;

		return -EIO;
	}

	if (addr == 0xf9) {
		u8 *buf = (u8 *)val_buf;

		if (buf[0] != 0xaa || buf[1] != 0xaa ||
		    buf[2] != val_size) {
			tsdata->header_errors++;
			dev_err_ratelimited(dev,
					    "Unexpected header: %02x%02x%02x\n",
					    buf[0], buf[1], buf[2]);
			return -EIO;
		}

		if (!edt_ft5x06_ts_check_crc(tsdata, val_buf, val_size))
			return -EIO;
	} else if (reg_read) {
		wbuf[2] = rbuf[0];
		wbuf[3] = rbuf[1];
		if (!edt_ft5x06_ts_check_crc(tsdata, wbuf, 4))
			return -EIO;

		*((u8 *)val_buf) = rbuf[0];
	}

	return 0;
}

static int edt_M06_i2c_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(i2c);
	u8 addr, val;
	u8 wbuf[4];
	struct i2c_msg xfer;
	int ret;

	addr = *((u8 *)data);
	val = *((u8 *)data + 1);

	wbuf[0] = M06_REG_CMD(tsdata->factory_mode);
	wbuf[1] = M06_REG_ADDR(tsdata->factory_mode, addr);
	wbuf[2] = val;
	wbuf[3] = wbuf[0] ^ wbuf[1] ^ wbuf[2];

	xfer.addr  = i2c->addr;
	xfer.flags = 0;
	xfer.len = 4;
	xfer.buf = wbuf;

	ret = i2c_transfer(i2c->adapter, &xfer, 1);
	if (ret != 1) {
		if (ret < 0)
			return ret;

		return -EIO;
	}

	return 0;
}

static const struct regmap_config edt_M06_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.read = edt_M06_i2c_read,
	.write = edt_M06_i2c_write,
};

static irqreturn_t edt_ft5x06_ts_isr(int irq, void *dev_id)
{
	struct edt_ft5x06_ts_data *tsdata = dev_id;
	struct device *dev = &tsdata->client->dev;
	u8 rdbuf[63];
	int i, type, x, y, id;
	int error;

	memset(rdbuf, 0, sizeof(rdbuf));
	error = regmap_bulk_read(tsdata->regmap, tsdata->tdata_cmd, rdbuf,
				 tsdata->tdata_len);
	if (error) {
		dev_err_ratelimited(dev, "Unable to fetch data, error: %d\n",
				    error);
		goto out;
	}

	for (i = 0; i < tsdata->max_support_points; i++) {
		u8 *buf = &rdbuf[i * tsdata->point_len + tsdata->tdata_offset];

		type = buf[0] >> 6;
		/* ignore Reserved events */
		if (type == TOUCH_EVENT_RESERVED)
			continue;

		/* M06 sometimes sends bogus coordinates in TOUCH_DOWN */
		if (tsdata->version == EDT_M06 && type == TOUCH_EVENT_DOWN)
			continue;

		x = get_unaligned_be16(buf) & 0x0fff;
		y = get_unaligned_be16(buf + 2) & 0x0fff;
		/* The FT5x26 send the y coordinate first */
		if (tsdata->version == EV_FT)
			swap(x, y);

		id = (buf[2] >> 4) & 0x0f;

		input_mt_slot(tsdata->input, id);
		if (input_mt_report_slot_state(tsdata->input, MT_TOOL_FINGER,
					       type != TOUCH_EVENT_UP))
			touchscreen_report_pos(tsdata->input, &tsdata->prop,
					       x, y, true);
	}

	input_mt_report_pointer_emulation(tsdata->input, true);
	input_sync(tsdata->input);

out:
	return IRQ_HANDLED;
}

struct edt_ft5x06_attribute {
	struct device_attribute dattr;
	size_t field_offset;
	u8 limit_low;
	u8 limit_high;
	u8 addr_m06;
	u8 addr_m09;
	u8 addr_ev;
};

#define EDT_ATTR(_field, _mode, _addr_m06, _addr_m09, _addr_ev,		\
		_limit_low, _limit_high)				\
	struct edt_ft5x06_attribute edt_ft5x06_attr_##_field = {	\
		.dattr = __ATTR(_field, _mode,				\
				edt_ft5x06_setting_show,		\
				edt_ft5x06_setting_store),		\
		.field_offset = offsetof(struct edt_ft5x06_ts_data, _field), \
		.addr_m06 = _addr_m06,					\
		.addr_m09 = _addr_m09,					\
		.addr_ev  = _addr_ev,					\
		.limit_low = _limit_low,				\
		.limit_high = _limit_high,				\
	}

static ssize_t edt_ft5x06_setting_show(struct device *dev,
				       struct device_attribute *dattr,
				       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);
	struct edt_ft5x06_attribute *attr =
			container_of(dattr, struct edt_ft5x06_attribute, dattr);
	u8 *field = (u8 *)tsdata + attr->field_offset;
	unsigned int val;
	size_t count = 0;
	int error = 0;
	u8 addr;

	mutex_lock(&tsdata->mutex);

	if (tsdata->factory_mode) {
		error = -EIO;
		goto out;
	}

	switch (tsdata->version) {
	case EDT_M06:
		addr = attr->addr_m06;
		break;

	case EDT_M09:
	case EDT_M12:
	case GENERIC_FT:
		addr = attr->addr_m09;
		break;

	case EV_FT:
		addr = attr->addr_ev;
		break;

	default:
		error = -ENODEV;
		goto out;
	}

	if (addr != NO_REGISTER) {
		error = regmap_read(tsdata->regmap, addr, &val);
		if (error) {
			dev_err(&tsdata->client->dev,
				"Failed to fetch attribute %s, error %d\n",
				dattr->attr.name, error);
			goto out;
		}
	} else {
		val = *field;
	}

	if (val != *field) {
		dev_warn(&tsdata->client->dev,
			 "%s: read (%d) and stored value (%d) differ\n",
			 dattr->attr.name, val, *field);
		*field = val;
	}

	count = sysfs_emit(buf, "%d\n", val);
out:
	mutex_unlock(&tsdata->mutex);
	return error ?: count;
}

static ssize_t edt_ft5x06_setting_store(struct device *dev,
					struct device_attribute *dattr,
					const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);
	struct edt_ft5x06_attribute *attr =
			container_of(dattr, struct edt_ft5x06_attribute, dattr);
	u8 *field = (u8 *)tsdata + attr->field_offset;
	unsigned int val;
	int error;
	u8 addr;

	mutex_lock(&tsdata->mutex);

	if (tsdata->factory_mode) {
		error = -EIO;
		goto out;
	}

	error = kstrtouint(buf, 0, &val);
	if (error)
		goto out;

	if (val < attr->limit_low || val > attr->limit_high) {
		error = -ERANGE;
		goto out;
	}

	switch (tsdata->version) {
	case EDT_M06:
		addr = attr->addr_m06;
		break;

	case EDT_M09:
	case EDT_M12:
	case GENERIC_FT:
		addr = attr->addr_m09;
		break;

	case EV_FT:
		addr = attr->addr_ev;
		break;

	default:
		error = -ENODEV;
		goto out;
	}

	if (addr != NO_REGISTER) {
		error = regmap_write(tsdata->regmap, addr, val);
		if (error) {
			dev_err(&tsdata->client->dev,
				"Failed to update attribute %s, error: %d\n",
				dattr->attr.name, error);
			goto out;
		}
	}
	*field = val;

out:
	mutex_unlock(&tsdata->mutex);
	return error ?: count;
}

/* m06, m09: range 0-31, m12: range 0-5 */
static EDT_ATTR(gain, S_IWUSR | S_IRUGO, WORK_REGISTER_GAIN,
		M09_REGISTER_GAIN, EV_REGISTER_GAIN, 0, 31);
/* m06, m09: range 0-31, m12: range 0-16 */
static EDT_ATTR(offset, S_IWUSR | S_IRUGO, WORK_REGISTER_OFFSET,
		M09_REGISTER_OFFSET, NO_REGISTER, 0, 31);
/* m06, m09, m12: no supported, ev_ft: range 0-80 */
static EDT_ATTR(offset_x, S_IWUSR | S_IRUGO, NO_REGISTER, NO_REGISTER,
		EV_REGISTER_OFFSET_X, 0, 80);
/* m06, m09, m12: no supported, ev_ft: range 0-80 */
static EDT_ATTR(offset_y, S_IWUSR | S_IRUGO, NO_REGISTER, NO_REGISTER,
		EV_REGISTER_OFFSET_Y, 0, 80);
/* m06: range 20 to 80, m09: range 0 to 30, m12: range 1 to 255... */
static EDT_ATTR(threshold, S_IWUSR | S_IRUGO, WORK_REGISTER_THRESHOLD,
		M09_REGISTER_THRESHOLD, EV_REGISTER_THRESHOLD, 0, 255);
/* m06: range 3 to 14, m12: range 1 to 255 */
static EDT_ATTR(report_rate, S_IWUSR | S_IRUGO, WORK_REGISTER_REPORT_RATE,
		M12_REGISTER_REPORT_RATE, NO_REGISTER, 0, 255);

static ssize_t model_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%s\n", tsdata->name);
}

static DEVICE_ATTR_RO(model);

static ssize_t fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%s\n", tsdata->fw_version);
}

static DEVICE_ATTR_RO(fw_version);

/* m06 only */
static ssize_t header_errors_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%d\n", tsdata->header_errors);
}

static DEVICE_ATTR_RO(header_errors);

/* m06 only */
static ssize_t crc_errors_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%d\n", tsdata->crc_errors);
}

static DEVICE_ATTR_RO(crc_errors);

static struct attribute *edt_ft5x06_attrs[] = {
	&edt_ft5x06_attr_gain.dattr.attr,
	&edt_ft5x06_attr_offset.dattr.attr,
	&edt_ft5x06_attr_offset_x.dattr.attr,
	&edt_ft5x06_attr_offset_y.dattr.attr,
	&edt_ft5x06_attr_threshold.dattr.attr,
	&edt_ft5x06_attr_report_rate.dattr.attr,
	&dev_attr_model.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_header_errors.attr,
	&dev_attr_crc_errors.attr,
	NULL
};
ATTRIBUTE_GROUPS(edt_ft5x06);

static void edt_ft5x06_restore_reg_parameters(struct edt_ft5x06_ts_data *tsdata)
{
	struct edt_reg_addr *reg_addr = &tsdata->reg_addr;
	struct regmap *regmap = tsdata->regmap;

	regmap_write(regmap, reg_addr->reg_threshold, tsdata->threshold);
	regmap_write(regmap, reg_addr->reg_gain, tsdata->gain);
	if (reg_addr->reg_offset != NO_REGISTER)
		regmap_write(regmap, reg_addr->reg_offset, tsdata->offset);
	if (reg_addr->reg_offset_x != NO_REGISTER)
		regmap_write(regmap, reg_addr->reg_offset_x, tsdata->offset_x);
	if (reg_addr->reg_offset_y != NO_REGISTER)
		regmap_write(regmap, reg_addr->reg_offset_y, tsdata->offset_y);
	if (reg_addr->reg_report_rate != NO_REGISTER)
		regmap_write(regmap, reg_addr->reg_report_rate,
			     tsdata->report_rate);
}

#ifdef CONFIG_DEBUG_FS
static int edt_ft5x06_factory_mode(struct edt_ft5x06_ts_data *tsdata)
{
	struct i2c_client *client = tsdata->client;
	int retries = EDT_SWITCH_MODE_RETRIES;
	unsigned int val;
	int error;

	if (tsdata->version != EDT_M06) {
		dev_err(&client->dev,
			"No factory mode support for non-M06 devices\n");
		return -EINVAL;
	}

	disable_irq(client->irq);

	if (!tsdata->raw_buffer) {
		tsdata->raw_bufsize = tsdata->num_x * tsdata->num_y *
				      sizeof(u16);
		tsdata->raw_buffer = kzalloc(tsdata->raw_bufsize, GFP_KERNEL);
		if (!tsdata->raw_buffer) {
			error = -ENOMEM;
			goto err_out;
		}
	}

	/* mode register is 0x3c when in the work mode */
	error = regmap_write(tsdata->regmap, WORK_REGISTER_OPMODE, 0x03);
	if (error) {
		dev_err(&client->dev,
			"failed to switch to factory mode, error %d\n", error);
		goto err_out;
	}

	tsdata->factory_mode = true;
	do {
		mdelay(EDT_SWITCH_MODE_DELAY);
		/* mode register is 0x01 when in factory mode */
		error = regmap_read(tsdata->regmap, FACTORY_REGISTER_OPMODE,
				    &val);
		if (!error && val == 0x03)
			break;
	} while (--retries > 0);

	if (retries == 0) {
		dev_err(&client->dev, "not in factory mode after %dms.\n",
			EDT_SWITCH_MODE_RETRIES * EDT_SWITCH_MODE_DELAY);
		error = -EIO;
		goto err_out;
	}

	return 0;

err_out:
	kfree(tsdata->raw_buffer);
	tsdata->raw_buffer = NULL;
	tsdata->factory_mode = false;
	enable_irq(client->irq);

	return error;
}

static int edt_ft5x06_work_mode(struct edt_ft5x06_ts_data *tsdata)
{
	struct i2c_client *client = tsdata->client;
	int retries = EDT_SWITCH_MODE_RETRIES;
	unsigned int val;
	int error;

	/* mode register is 0x01 when in the factory mode */
	error = regmap_write(tsdata->regmap, FACTORY_REGISTER_OPMODE, 0x1);
	if (error) {
		dev_err(&client->dev,
			"failed to switch to work mode, error: %d\n", error);
		return error;
	}

	tsdata->factory_mode = false;

	do {
		mdelay(EDT_SWITCH_MODE_DELAY);
		/* mode register is 0x01 when in factory mode */
		error = regmap_read(tsdata->regmap, WORK_REGISTER_OPMODE, &val);
		if (!error && val == 0x01)
			break;
	} while (--retries > 0);

	if (retries == 0) {
		dev_err(&client->dev, "not in work mode after %dms.\n",
			EDT_SWITCH_MODE_RETRIES * EDT_SWITCH_MODE_DELAY);
		tsdata->factory_mode = true;
		return -EIO;
	}

	kfree(tsdata->raw_buffer);
	tsdata->raw_buffer = NULL;

	edt_ft5x06_restore_reg_parameters(tsdata);
	enable_irq(client->irq);

	return 0;
}

static int edt_ft5x06_debugfs_mode_get(void *data, u64 *mode)
{
	struct edt_ft5x06_ts_data *tsdata = data;

	*mode = tsdata->factory_mode;

	return 0;
};

static int edt_ft5x06_debugfs_mode_set(void *data, u64 mode)
{
	struct edt_ft5x06_ts_data *tsdata = data;
	int retval = 0;

	if (mode > 1)
		return -ERANGE;

	mutex_lock(&tsdata->mutex);

	if (mode != tsdata->factory_mode) {
		retval = mode ? edt_ft5x06_factory_mode(tsdata) :
				edt_ft5x06_work_mode(tsdata);
	}

	mutex_unlock(&tsdata->mutex);

	return retval;
};

DEFINE_SIMPLE_ATTRIBUTE(debugfs_mode_fops, edt_ft5x06_debugfs_mode_get,
			edt_ft5x06_debugfs_mode_set, "%llu\n");

static ssize_t edt_ft5x06_debugfs_raw_data_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *off)
{
	struct edt_ft5x06_ts_data *tsdata = file->private_data;
	struct i2c_client *client = tsdata->client;
	int retries  = EDT_RAW_DATA_RETRIES;
	unsigned int val;
	int i, error;
	size_t read = 0;
	int colbytes;
	u8 *rdbuf;

	if (*off < 0 || *off >= tsdata->raw_bufsize)
		return 0;

	mutex_lock(&tsdata->mutex);

	if (!tsdata->factory_mode || !tsdata->raw_buffer) {
		error = -EIO;
		goto out;
	}

	error = regmap_write(tsdata->regmap, 0x08, 0x01);
	if (error) {
		dev_err(&client->dev,
			"failed to write 0x08 register, error %d\n", error);
		goto out;
	}

	do {
		usleep_range(EDT_RAW_DATA_DELAY, EDT_RAW_DATA_DELAY + 100);
		error = regmap_read(tsdata->regmap, 0x08, &val);
		if (error) {
			dev_err(&client->dev,
				"failed to read 0x08 register, error %d\n",
				error);
			goto out;
		}

		if (val == 1)
			break;
	} while (--retries > 0);

	if (retries == 0) {
		dev_err(&client->dev,
			"timed out waiting for register to settle\n");
		error = -ETIMEDOUT;
		goto out;
	}

	rdbuf = tsdata->raw_buffer;
	colbytes = tsdata->num_y * sizeof(u16);

	for (i = 0; i < tsdata->num_x; i++) {
		rdbuf[0] = i;  /* column index */
		error = regmap_bulk_read(tsdata->regmap, 0xf5, rdbuf, colbytes);
		if (error)
			goto out;

		rdbuf += colbytes;
	}

	read = min_t(size_t, count, tsdata->raw_bufsize - *off);
	if (copy_to_user(buf, tsdata->raw_buffer + *off, read)) {
		error = -EFAULT;
		goto out;
	}

	*off += read;
out:
	mutex_unlock(&tsdata->mutex);
	return error ?: read;
};

static const struct file_operations debugfs_raw_data_fops = {
	.open = simple_open,
	.read = edt_ft5x06_debugfs_raw_data_read,
};

static void edt_ft5x06_ts_prepare_debugfs(struct edt_ft5x06_ts_data *tsdata,
					  const char *debugfs_name)
{
	tsdata->debug_dir = debugfs_create_dir(debugfs_name, NULL);

	debugfs_create_u16("num_x", S_IRUSR, tsdata->debug_dir, &tsdata->num_x);
	debugfs_create_u16("num_y", S_IRUSR, tsdata->debug_dir, &tsdata->num_y);

	debugfs_create_file("mode", S_IRUSR | S_IWUSR,
			    tsdata->debug_dir, tsdata, &debugfs_mode_fops);
	debugfs_create_file("raw_data", S_IRUSR,
			    tsdata->debug_dir, tsdata, &debugfs_raw_data_fops);
}

static void edt_ft5x06_ts_teardown_debugfs(struct edt_ft5x06_ts_data *tsdata)
{
	debugfs_remove_recursive(tsdata->debug_dir);
	kfree(tsdata->raw_buffer);
}

#else

static int edt_ft5x06_factory_mode(struct edt_ft5x06_ts_data *tsdata)
{
	return -ENOSYS;
}

static void edt_ft5x06_ts_prepare_debugfs(struct edt_ft5x06_ts_data *tsdata,
					  const char *debugfs_name)
{
}

static void edt_ft5x06_ts_teardown_debugfs(struct edt_ft5x06_ts_data *tsdata)
{
}

#endif /* CONFIG_DEBUGFS */

static int edt_ft5x06_ts_identify(struct i2c_client *client,
				  struct edt_ft5x06_ts_data *tsdata)
{
	u8 rdbuf[EDT_NAME_LEN];
	char *p;
	int error;
	char *model_name = tsdata->name;
	char *fw_version = tsdata->fw_version;

	/* see what we find if we assume it is a M06 *
	 * if we get less than EDT_NAME_LEN, we don't want
	 * to have garbage in there
	 */
	memset(rdbuf, 0, sizeof(rdbuf));
	error = regmap_bulk_read(tsdata->regmap, 0xBB, rdbuf, EDT_NAME_LEN - 1);
	if (error)
		return error;

	/* Probe content for something consistent.
	 * M06 starts with a response byte, M12 gives the data directly.
	 * M09/Generic does not provide model number information.
	 */
	if (!strncasecmp(rdbuf + 1, "EP0", 3)) {
		tsdata->version = EDT_M06;

		/* remove last '$' end marker */
		rdbuf[EDT_NAME_LEN - 1] = '\0';
		if (rdbuf[EDT_NAME_LEN - 2] == '$')
			rdbuf[EDT_NAME_LEN - 2] = '\0';

		/* look for Model/Version separator */
		p = strchr(rdbuf, '*');
		if (p)
			*p++ = '\0';
		strscpy(model_name, rdbuf + 1, EDT_NAME_LEN);
		strscpy(fw_version, p ? p : "", EDT_NAME_LEN);

		regmap_exit(tsdata->regmap);
		tsdata->regmap = regmap_init_i2c(client,
						 &edt_M06_i2c_regmap_config);
		if (IS_ERR(tsdata->regmap)) {
			dev_err(&client->dev, "regmap allocation failed\n");
			return PTR_ERR(tsdata->regmap);
		}
	} else if (!strncasecmp(rdbuf, "EP0", 3)) {
		tsdata->version = EDT_M12;

		/* remove last '$' end marker */
		rdbuf[EDT_NAME_LEN - 2] = '\0';
		if (rdbuf[EDT_NAME_LEN - 3] == '$')
			rdbuf[EDT_NAME_LEN - 3] = '\0';

		/* look for Model/Version separator */
		p = strchr(rdbuf, '*');
		if (p)
			*p++ = '\0';
		strscpy(model_name, rdbuf, EDT_NAME_LEN);
		strscpy(fw_version, p ? p : "", EDT_NAME_LEN);
	} else {
		/* If it is not an EDT M06/M12 touchscreen, then the model
		 * detection is a bit hairy. The different ft5x06
		 * firmwares around don't reliably implement the
		 * identification registers. Well, we'll take a shot.
		 *
		 * The main difference between generic focaltec based
		 * touches and EDT M09 is that we know how to retrieve
		 * the max coordinates for the latter.
		 */
		tsdata->version = GENERIC_FT;

		error = regmap_bulk_read(tsdata->regmap, 0xA6, rdbuf, 2);
		if (error)
			return error;

		strscpy(fw_version, rdbuf, 2);

		error = regmap_bulk_read(tsdata->regmap, 0xA8, rdbuf, 1);
		if (error)
			return error;

		/* This "model identification" is not exact. Unfortunately
		 * not all firmwares for the ft5x06 put useful values in
		 * the identification registers.
		 */
		switch (rdbuf[0]) {
		case 0x11:   /* EDT EP0110M09 */
		case 0x35:   /* EDT EP0350M09 */
		case 0x43:   /* EDT EP0430M09 */
		case 0x50:   /* EDT EP0500M09 */
		case 0x57:   /* EDT EP0570M09 */
		case 0x70:   /* EDT EP0700M09 */
			tsdata->version = EDT_M09;
			snprintf(model_name, EDT_NAME_LEN, "EP0%i%i0M09",
				 rdbuf[0] >> 4, rdbuf[0] & 0x0F);
			break;
		case 0xa1:   /* EDT EP1010ML00 */
			tsdata->version = EDT_M09;
			snprintf(model_name, EDT_NAME_LEN, "EP%i%i0ML00",
				 rdbuf[0] >> 4, rdbuf[0] & 0x0F);
			break;
		case 0x5a:   /* Solomon Goldentek Display */
			snprintf(model_name, EDT_NAME_LEN, "GKTW50SCED1R0");
			break;
		case 0x59:  /* Evervision Display with FT5xx6 TS */
			tsdata->version = EV_FT;
			error = regmap_bulk_read(tsdata->regmap, 0x53, rdbuf, 1);
			if (error)
				return error;
			strscpy(fw_version, rdbuf, 1);
			snprintf(model_name, EDT_NAME_LEN,
				 "EVERVISION-FT5726NEi");
			break;
		default:
			snprintf(model_name, EDT_NAME_LEN,
				 "generic ft5x06 (%02x)",
				 rdbuf[0]);
			break;
		}
	}

	return 0;
}

static void edt_ft5x06_ts_get_defaults(struct device *dev,
				       struct edt_ft5x06_ts_data *tsdata)
{
	struct edt_reg_addr *reg_addr = &tsdata->reg_addr;
	struct regmap *regmap = tsdata->regmap;
	u32 val;
	int error;

	error = device_property_read_u32(dev, "threshold", &val);
	if (!error) {
		regmap_write(regmap, reg_addr->reg_threshold, val);
		tsdata->threshold = val;
	}

	error = device_property_read_u32(dev, "gain", &val);
	if (!error) {
		regmap_write(regmap, reg_addr->reg_gain, val);
		tsdata->gain = val;
	}

	error = device_property_read_u32(dev, "offset", &val);
	if (!error) {
		if (reg_addr->reg_offset != NO_REGISTER)
			regmap_write(regmap, reg_addr->reg_offset, val);
		tsdata->offset = val;
	}

	error = device_property_read_u32(dev, "offset-x", &val);
	if (!error) {
		if (reg_addr->reg_offset_x != NO_REGISTER)
			regmap_write(regmap, reg_addr->reg_offset_x, val);
		tsdata->offset_x = val;
	}

	error = device_property_read_u32(dev, "offset-y", &val);
	if (!error) {
		if (reg_addr->reg_offset_y != NO_REGISTER)
			regmap_write(regmap, reg_addr->reg_offset_y, val);
		tsdata->offset_y = val;
	}
}

static void edt_ft5x06_ts_get_parameters(struct edt_ft5x06_ts_data *tsdata)
{
	struct edt_reg_addr *reg_addr = &tsdata->reg_addr;
	struct regmap *regmap = tsdata->regmap;
	unsigned int val;

	regmap_read(regmap, reg_addr->reg_threshold, &tsdata->threshold);
	regmap_read(regmap, reg_addr->reg_gain, &tsdata->gain);
	if (reg_addr->reg_offset != NO_REGISTER)
		regmap_read(regmap, reg_addr->reg_offset, &tsdata->offset);
	if (reg_addr->reg_offset_x != NO_REGISTER)
		regmap_read(regmap, reg_addr->reg_offset_x, &tsdata->offset_x);
	if (reg_addr->reg_offset_y != NO_REGISTER)
		regmap_read(regmap, reg_addr->reg_offset_y, &tsdata->offset_y);
	if (reg_addr->reg_report_rate != NO_REGISTER)
		regmap_read(regmap, reg_addr->reg_report_rate,
			    &tsdata->report_rate);
	tsdata->num_x = EDT_DEFAULT_NUM_X;
	if (reg_addr->reg_num_x != NO_REGISTER) {
		if (!regmap_read(regmap, reg_addr->reg_num_x, &val))
			tsdata->num_x = val;
	}
	tsdata->num_y = EDT_DEFAULT_NUM_Y;
	if (reg_addr->reg_num_y != NO_REGISTER) {
		if (!regmap_read(regmap, reg_addr->reg_num_y, &val))
			tsdata->num_y = val;
	}
}

static void edt_ft5x06_ts_set_tdata_parameters(struct edt_ft5x06_ts_data *tsdata)
{
	int crclen;

	if (tsdata->version == EDT_M06) {
		tsdata->tdata_cmd = 0xf9;
		tsdata->tdata_offset = 5;
		tsdata->point_len = 4;
		crclen = 1;
	} else {
		tsdata->tdata_cmd = 0x0;
		tsdata->tdata_offset = 3;
		tsdata->point_len = 6;
		crclen = 0;
	}

	tsdata->tdata_len = tsdata->point_len * tsdata->max_support_points +
		tsdata->tdata_offset + crclen;
}

static void edt_ft5x06_ts_set_regs(struct edt_ft5x06_ts_data *tsdata)
{
	struct edt_reg_addr *reg_addr = &tsdata->reg_addr;

	switch (tsdata->version) {
	case EDT_M06:
		reg_addr->reg_threshold = WORK_REGISTER_THRESHOLD;
		reg_addr->reg_report_rate = WORK_REGISTER_REPORT_RATE;
		reg_addr->reg_gain = WORK_REGISTER_GAIN;
		reg_addr->reg_offset = WORK_REGISTER_OFFSET;
		reg_addr->reg_offset_x = NO_REGISTER;
		reg_addr->reg_offset_y = NO_REGISTER;
		reg_addr->reg_num_x = WORK_REGISTER_NUM_X;
		reg_addr->reg_num_y = WORK_REGISTER_NUM_Y;
		break;

	case EDT_M09:
	case EDT_M12:
		reg_addr->reg_threshold = M09_REGISTER_THRESHOLD;
		reg_addr->reg_report_rate = tsdata->version == EDT_M12 ?
			M12_REGISTER_REPORT_RATE : NO_REGISTER;
		reg_addr->reg_gain = M09_REGISTER_GAIN;
		reg_addr->reg_offset = M09_REGISTER_OFFSET;
		reg_addr->reg_offset_x = NO_REGISTER;
		reg_addr->reg_offset_y = NO_REGISTER;
		reg_addr->reg_num_x = M09_REGISTER_NUM_X;
		reg_addr->reg_num_y = M09_REGISTER_NUM_Y;
		break;

	case EV_FT:
		reg_addr->reg_threshold = EV_REGISTER_THRESHOLD;
		reg_addr->reg_report_rate = NO_REGISTER;
		reg_addr->reg_gain = EV_REGISTER_GAIN;
		reg_addr->reg_offset = NO_REGISTER;
		reg_addr->reg_offset_x = EV_REGISTER_OFFSET_X;
		reg_addr->reg_offset_y = EV_REGISTER_OFFSET_Y;
		reg_addr->reg_num_x = NO_REGISTER;
		reg_addr->reg_num_y = NO_REGISTER;
		break;

	case GENERIC_FT:
		/* this is a guesswork */
		reg_addr->reg_threshold = M09_REGISTER_THRESHOLD;
		reg_addr->reg_report_rate = NO_REGISTER;
		reg_addr->reg_gain = M09_REGISTER_GAIN;
		reg_addr->reg_offset = M09_REGISTER_OFFSET;
		reg_addr->reg_offset_x = NO_REGISTER;
		reg_addr->reg_offset_y = NO_REGISTER;
		reg_addr->reg_num_x = NO_REGISTER;
		reg_addr->reg_num_y = NO_REGISTER;
		break;
	}
}

static void edt_ft5x06_exit_regmap(void *arg)
{
	struct edt_ft5x06_ts_data *data = arg;

	if (!IS_ERR_OR_NULL(data->regmap))
		regmap_exit(data->regmap);
}

static void edt_ft5x06_disable_regulators(void *arg)
{
	struct edt_ft5x06_ts_data *data = arg;

	regulator_disable(data->vcc);
	regulator_disable(data->iovcc);
}

static int edt_ft5x06_ts_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	const struct edt_i2c_chip_data *chip_data;
	struct edt_ft5x06_ts_data *tsdata;
	unsigned int val;
	struct input_dev *input;
	unsigned long irq_flags;
	int error;
	u32 report_rate;

	dev_dbg(&client->dev, "probing for EDT FT5x06 I2C\n");

	tsdata = devm_kzalloc(&client->dev, sizeof(*tsdata), GFP_KERNEL);
	if (!tsdata) {
		dev_err(&client->dev, "failed to allocate driver data.\n");
		return -ENOMEM;
	}

	tsdata->regmap = regmap_init_i2c(client, &edt_ft5x06_i2c_regmap_config);
	if (IS_ERR(tsdata->regmap)) {
		dev_err(&client->dev, "regmap allocation failed\n");
		return PTR_ERR(tsdata->regmap);
	}

	/*
	 * We are not using devm_regmap_init_i2c() and instead install a
	 * custom action because we may replace regmap with M06-specific one
	 * and we need to make sure that it will not be released too early.
	 */
	error = devm_add_action_or_reset(&client->dev, edt_ft5x06_exit_regmap,
					 tsdata);
	if (error)
		return error;

	chip_data = device_get_match_data(&client->dev);
	if (!chip_data)
		chip_data = (const struct edt_i2c_chip_data *)id->driver_data;
	if (!chip_data || !chip_data->max_support_points) {
		dev_err(&client->dev, "invalid or missing chip data\n");
		return -EINVAL;
	}

	tsdata->max_support_points = chip_data->max_support_points;

	tsdata->vcc = devm_regulator_get(&client->dev, "vcc");
	if (IS_ERR(tsdata->vcc))
		return dev_err_probe(&client->dev, PTR_ERR(tsdata->vcc),
				     "failed to request regulator\n");

	tsdata->iovcc = devm_regulator_get(&client->dev, "iovcc");
	if (IS_ERR(tsdata->iovcc)) {
		error = PTR_ERR(tsdata->iovcc);
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev,
				"failed to request iovcc regulator: %d\n", error);
		return error;
	}

	error = regulator_enable(tsdata->iovcc);
	if (error < 0) {
		dev_err(&client->dev, "failed to enable iovcc: %d\n", error);
		return error;
	}

	/* Delay enabling VCC for > 10us (T_ivd) after IOVCC */
	usleep_range(10, 100);

	error = regulator_enable(tsdata->vcc);
	if (error < 0) {
		dev_err(&client->dev, "failed to enable vcc: %d\n", error);
		regulator_disable(tsdata->iovcc);
		return error;
	}

	error = devm_add_action_or_reset(&client->dev,
					 edt_ft5x06_disable_regulators,
					 tsdata);
	if (error)
		return error;

	tsdata->reset_gpio = devm_gpiod_get_optional(&client->dev,
						     "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(tsdata->reset_gpio)) {
		error = PTR_ERR(tsdata->reset_gpio);
		dev_err(&client->dev,
			"Failed to request GPIO reset pin, error %d\n", error);
		return error;
	}

	tsdata->wake_gpio = devm_gpiod_get_optional(&client->dev,
						    "wake", GPIOD_OUT_LOW);
	if (IS_ERR(tsdata->wake_gpio)) {
		error = PTR_ERR(tsdata->wake_gpio);
		dev_err(&client->dev,
			"Failed to request GPIO wake pin, error %d\n", error);
		return error;
	}

	/*
	 * Check which sleep modes we can support. Power-off requieres the
	 * reset-pin to ensure correct power-down/power-up behaviour. Start with
	 * the EDT_PMODE_POWEROFF test since this is the deepest possible sleep
	 * mode.
	 */
	if (tsdata->reset_gpio)
		tsdata->suspend_mode = EDT_PMODE_POWEROFF;
	else if (tsdata->wake_gpio)
		tsdata->suspend_mode = EDT_PMODE_HIBERNATE;
	else
		tsdata->suspend_mode = EDT_PMODE_NOT_SUPPORTED;

	if (tsdata->wake_gpio) {
		usleep_range(5000, 6000);
		gpiod_set_value_cansleep(tsdata->wake_gpio, 1);
		usleep_range(5000, 6000);
	}

	if (tsdata->reset_gpio) {
		usleep_range(5000, 6000);
		gpiod_set_value_cansleep(tsdata->reset_gpio, 0);
		msleep(300);
	}

	input = devm_input_allocate_device(&client->dev);
	if (!input) {
		dev_err(&client->dev, "failed to allocate input device.\n");
		return -ENOMEM;
	}

	mutex_init(&tsdata->mutex);
	tsdata->client = client;
	tsdata->input = input;
	tsdata->factory_mode = false;
	i2c_set_clientdata(client, tsdata);

	error = edt_ft5x06_ts_identify(client, tsdata);
	if (error) {
		dev_err(&client->dev, "touchscreen probe failed\n");
		return error;
	}

	/*
	 * Dummy read access. EP0700MLP1 returns bogus data on the first
	 * register read access and ignores writes.
	 */
	regmap_read(tsdata->regmap, 0x00, &val);

	edt_ft5x06_ts_set_tdata_parameters(tsdata);
	edt_ft5x06_ts_set_regs(tsdata);
	edt_ft5x06_ts_get_defaults(&client->dev, tsdata);
	edt_ft5x06_ts_get_parameters(tsdata);

	if (tsdata->reg_addr.reg_report_rate != NO_REGISTER &&
	    !device_property_read_u32(&client->dev,
				      "report-rate-hz", &report_rate)) {
		if (tsdata->version == EDT_M06)
			tsdata->report_rate = clamp_val(report_rate, 30, 140);
		else
			tsdata->report_rate = clamp_val(report_rate, 1, 255);

		if (report_rate != tsdata->report_rate)
			dev_warn(&client->dev,
				 "report-rate %dHz is unsupported, use %dHz\n",
				 report_rate, tsdata->report_rate);

		if (tsdata->version == EDT_M06)
			tsdata->report_rate /= 10;

		regmap_write(tsdata->regmap, tsdata->reg_addr.reg_report_rate,
			     tsdata->report_rate);
	}

	dev_dbg(&client->dev,
		"Model \"%s\", Rev. \"%s\", %dx%d sensors\n",
		tsdata->name, tsdata->fw_version, tsdata->num_x, tsdata->num_y);

	input->name = tsdata->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	input_set_abs_params(input, ABS_MT_POSITION_X,
			     0, tsdata->num_x * 64 - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y,
			     0, tsdata->num_y * 64 - 1, 0, 0);

	touchscreen_parse_properties(input, true, &tsdata->prop);

	error = input_mt_init_slots(input, tsdata->max_support_points,
				    INPUT_MT_DIRECT);
	if (error) {
		dev_err(&client->dev, "Unable to init MT slots.\n");
		return error;
	}

	irq_flags = irq_get_trigger_type(client->irq);
	if (irq_flags == IRQF_TRIGGER_NONE)
		irq_flags = IRQF_TRIGGER_FALLING;
	irq_flags |= IRQF_ONESHOT;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, edt_ft5x06_ts_isr, irq_flags,
					  client->name, tsdata);
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		return error;
	}

	error = input_register_device(input);
	if (error)
		return error;

	edt_ft5x06_ts_prepare_debugfs(tsdata, dev_driver_string(&client->dev));

	dev_dbg(&client->dev,
		"EDT FT5x06 initialized: IRQ %d, WAKE pin %d, Reset pin %d.\n",
		client->irq,
		tsdata->wake_gpio ? desc_to_gpio(tsdata->wake_gpio) : -1,
		tsdata->reset_gpio ? desc_to_gpio(tsdata->reset_gpio) : -1);

	return 0;
}

static void edt_ft5x06_ts_remove(struct i2c_client *client)
{
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);

	edt_ft5x06_ts_teardown_debugfs(tsdata);
}

static int edt_ft5x06_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);
	struct gpio_desc *reset_gpio = tsdata->reset_gpio;
	int ret;

	if (device_may_wakeup(dev))
		return 0;

	if (tsdata->suspend_mode == EDT_PMODE_NOT_SUPPORTED)
		return 0;

	/* Enter hibernate mode. */
	ret = regmap_write(tsdata->regmap, PMOD_REGISTER_OPMODE,
			   PMOD_REGISTER_HIBERNATE);
	if (ret)
		dev_warn(dev, "Failed to set hibernate mode\n");

	if (tsdata->suspend_mode == EDT_PMODE_HIBERNATE)
		return 0;

	/*
	 * Power-off according the datasheet. Cut the power may leaf the irq
	 * line in an undefined state depending on the host pull resistor
	 * settings. Disable the irq to avoid adjusting each host till the
	 * device is back in a full functional state.
	 */
	disable_irq(tsdata->client->irq);

	gpiod_set_value_cansleep(reset_gpio, 1);
	usleep_range(1000, 2000);

	ret = regulator_disable(tsdata->vcc);
	if (ret)
		dev_warn(dev, "Failed to disable vcc\n");
	ret = regulator_disable(tsdata->iovcc);
	if (ret)
		dev_warn(dev, "Failed to disable iovcc\n");

	return 0;
}

static int edt_ft5x06_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct edt_ft5x06_ts_data *tsdata = i2c_get_clientdata(client);
	int ret = 0;

	if (device_may_wakeup(dev))
		return 0;

	if (tsdata->suspend_mode == EDT_PMODE_NOT_SUPPORTED)
		return 0;

	if (tsdata->suspend_mode == EDT_PMODE_POWEROFF) {
		struct gpio_desc *reset_gpio = tsdata->reset_gpio;

		/*
		 * We can't check if the regulator is a dummy or a real
		 * regulator. So we need to specify the 5ms reset time (T_rst)
		 * here instead of the 100us T_rtp time. We also need to wait
		 * 300ms in case it was a real supply and the power was cutted
		 * of. Toggle the reset pin is also a way to exit the hibernate
		 * mode.
		 */
		gpiod_set_value_cansleep(reset_gpio, 1);
		usleep_range(5000, 6000);

		ret = regulator_enable(tsdata->iovcc);
		if (ret) {
			dev_err(dev, "Failed to enable iovcc\n");
			return ret;
		}

		/* Delay enabling VCC for > 10us (T_ivd) after IOVCC */
		usleep_range(10, 100);

		ret = regulator_enable(tsdata->vcc);
		if (ret) {
			dev_err(dev, "Failed to enable vcc\n");
			regulator_disable(tsdata->iovcc);
			return ret;
		}

		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(reset_gpio, 0);
		msleep(300);

		edt_ft5x06_restore_reg_parameters(tsdata);
		enable_irq(tsdata->client->irq);

		if (tsdata->factory_mode)
			ret = edt_ft5x06_factory_mode(tsdata);
	} else {
		struct gpio_desc *wake_gpio = tsdata->wake_gpio;

		gpiod_set_value_cansleep(wake_gpio, 0);
		usleep_range(5000, 6000);
		gpiod_set_value_cansleep(wake_gpio, 1);
	}

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(edt_ft5x06_ts_pm_ops,
				edt_ft5x06_ts_suspend, edt_ft5x06_ts_resume);

static const struct edt_i2c_chip_data edt_ft5x06_data = {
	.max_support_points = 5,
};

static const struct edt_i2c_chip_data edt_ft5452_data = {
	.max_support_points = 5,
};

static const struct edt_i2c_chip_data edt_ft5506_data = {
	.max_support_points = 10,
};

static const struct edt_i2c_chip_data edt_ft6236_data = {
	.max_support_points = 2,
};

static const struct edt_i2c_chip_data edt_ft8201_data = {
	.max_support_points = 10,
};

static const struct edt_i2c_chip_data edt_ft8719_data = {
	.max_support_points = 10,
};

static const struct i2c_device_id edt_ft5x06_ts_id[] = {
	{ .name = "edt-ft5x06", .driver_data = (long)&edt_ft5x06_data },
	{ .name = "edt-ft5506", .driver_data = (long)&edt_ft5506_data },
	{ .name = "ev-ft5726", .driver_data = (long)&edt_ft5506_data },
	{ .name = "ft5452", .driver_data = (long)&edt_ft5452_data },
	/* Note no edt- prefix for compatibility with the ft6236.c driver */
	{ .name = "ft6236", .driver_data = (long)&edt_ft6236_data },
	{ .name = "ft8201", .driver_data = (long)&edt_ft8201_data },
	{ .name = "ft8719", .driver_data = (long)&edt_ft8719_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, edt_ft5x06_ts_id);

static const struct of_device_id edt_ft5x06_of_match[] = {
	{ .compatible = "edt,edt-ft5206", .data = &edt_ft5x06_data },
	{ .compatible = "edt,edt-ft5306", .data = &edt_ft5x06_data },
	{ .compatible = "edt,edt-ft5406", .data = &edt_ft5x06_data },
	{ .compatible = "edt,edt-ft5506", .data = &edt_ft5506_data },
	{ .compatible = "evervision,ev-ft5726", .data = &edt_ft5506_data },
	{ .compatible = "focaltech,ft5426", .data = &edt_ft5506_data },
	{ .compatible = "focaltech,ft5452", .data = &edt_ft5452_data },
	/* Note focaltech vendor prefix for compatibility with ft6236.c */
	{ .compatible = "focaltech,ft6236", .data = &edt_ft6236_data },
	{ .compatible = "focaltech,ft8201", .data = &edt_ft8201_data },
	{ .compatible = "focaltech,ft8719", .data = &edt_ft8719_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, edt_ft5x06_of_match);

static struct i2c_driver edt_ft5x06_ts_driver = {
	.driver = {
		.name = "edt_ft5x06",
		.dev_groups = edt_ft5x06_groups,
		.of_match_table = edt_ft5x06_of_match,
		.pm = pm_sleep_ptr(&edt_ft5x06_ts_pm_ops),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = edt_ft5x06_ts_id,
	.probe    = edt_ft5x06_ts_probe,
	.remove   = edt_ft5x06_ts_remove,
};

module_i2c_driver(edt_ft5x06_ts_driver);

MODULE_AUTHOR("Simon Budig <simon.budig@kernelconcepts.de>");
MODULE_DESCRIPTION("EDT FT5x06 I2C Touchscreen Driver");
MODULE_LICENSE("GPL v2");
