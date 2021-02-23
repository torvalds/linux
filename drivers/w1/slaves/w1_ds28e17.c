// SPDX-License-Identifier: GPL-2.0-only
/*
 *	w1_ds28e17.c - w1 family 19 (DS28E17) driver
 *
 * Copyright (c) 2016 Jan Kandziora <jjj@gmx.de>
 */

#include <linux/crc16.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define CRC16_INIT 0

#include <linux/w1.h>

#define W1_FAMILY_DS28E17 0x19

/* Module setup. */
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jan Kandziora <jjj@gmx.de>");
MODULE_DESCRIPTION("w1 family 19 driver for DS28E17, 1-wire to I2C master bridge");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS28E17));


/* Default I2C speed to be set when a DS28E17 is detected. */
static int i2c_speed = 100;
module_param_named(speed, i2c_speed, int, (S_IRUSR | S_IWUSR));
MODULE_PARM_DESC(speed, "Default I2C speed to be set when a DS28E17 is detected");

/* Default I2C stretch value to be set when a DS28E17 is detected. */
static char i2c_stretch = 1;
module_param_named(stretch, i2c_stretch, byte, (S_IRUSR | S_IWUSR));
MODULE_PARM_DESC(stretch, "Default I2C stretch value to be set when a DS28E17 is detected");

/* DS28E17 device command codes. */
#define W1_F19_WRITE_DATA_WITH_STOP      0x4B
#define W1_F19_WRITE_DATA_NO_STOP        0x5A
#define W1_F19_WRITE_DATA_ONLY           0x69
#define W1_F19_WRITE_DATA_ONLY_WITH_STOP 0x78
#define W1_F19_READ_DATA_WITH_STOP       0x87
#define W1_F19_WRITE_READ_DATA_WITH_STOP 0x2D
#define W1_F19_WRITE_CONFIGURATION       0xD2
#define W1_F19_READ_CONFIGURATION        0xE1
#define W1_F19_ENABLE_SLEEP_MODE         0x1E
#define W1_F19_READ_DEVICE_REVISION      0xC4

/* DS28E17 status bits */
#define W1_F19_STATUS_CRC     0x01
#define W1_F19_STATUS_ADDRESS 0x02
#define W1_F19_STATUS_START   0x08

/*
 * Maximum number of I2C bytes to transfer within one CRC16 protected onewire
 * command.
 * */
#define W1_F19_WRITE_DATA_LIMIT 255

/* Maximum number of I2C bytes to read with one onewire command. */
#define W1_F19_READ_DATA_LIMIT 255

/* Constants for calculating the busy sleep. */
#define W1_F19_BUSY_TIMEBASES { 90, 23, 10 }
#define W1_F19_BUSY_GRATUITY  1000

/* Number of checks for the busy flag before timeout. */
#define W1_F19_BUSY_CHECKS 1000


/* Slave specific data. */
struct w1_f19_data {
	u8 speed;
	u8 stretch;
	struct i2c_adapter adapter;
};


/* Wait a while until the busy flag clears. */
static int w1_f19_i2c_busy_wait(struct w1_slave *sl, size_t count)
{
	const unsigned long timebases[3] = W1_F19_BUSY_TIMEBASES;
	struct w1_f19_data *data = sl->family_data;
	unsigned int checks;

	/* Check the busy flag first in any case.*/
	if (w1_touch_bit(sl->master, 1) == 0)
		return 0;

	/*
	 * Do a generously long sleep in the beginning,
	 * as we have to wait at least this time for all
	 * the I2C bytes at the given speed to be transferred.
	 */
	usleep_range(timebases[data->speed] * (data->stretch) * count,
		timebases[data->speed] * (data->stretch) * count
		+ W1_F19_BUSY_GRATUITY);

	/* Now continusly check the busy flag sent by the DS28E17. */
	checks = W1_F19_BUSY_CHECKS;
	while ((checks--) > 0) {
		/* Return success if the busy flag is cleared. */
		if (w1_touch_bit(sl->master, 1) == 0)
			return 0;

		/* Wait one non-streched byte timeslot. */
		udelay(timebases[data->speed]);
	}

	/* Timeout. */
	dev_warn(&sl->dev, "busy timeout\n");
	return -ETIMEDOUT;
}


/* Utility function: result. */
static size_t w1_f19_error(struct w1_slave *sl, u8 w1_buf[])
{
	/* Warnings. */
	if (w1_buf[0] & W1_F19_STATUS_CRC)
		dev_warn(&sl->dev, "crc16 mismatch\n");
	if (w1_buf[0] & W1_F19_STATUS_ADDRESS)
		dev_warn(&sl->dev, "i2c device not responding\n");
	if ((w1_buf[0] & (W1_F19_STATUS_CRC | W1_F19_STATUS_ADDRESS)) == 0
			&& w1_buf[1] != 0) {
		dev_warn(&sl->dev, "i2c short write, %d bytes not acknowledged\n",
			w1_buf[1]);
	}

	/* Check error conditions. */
	if (w1_buf[0] & W1_F19_STATUS_ADDRESS)
		return -ENXIO;
	if (w1_buf[0] & W1_F19_STATUS_START)
		return -EAGAIN;
	if (w1_buf[0] != 0 || w1_buf[1] != 0)
		return -EIO;

	/* All ok. */
	return 0;
}


/* Utility function: write data to I2C slave, single chunk. */
static int __w1_f19_i2c_write(struct w1_slave *sl,
	const u8 *command, size_t command_count,
	const u8 *buffer, size_t count)
{
	u16 crc;
	int error;
	u8 w1_buf[2];

	/* Send command and I2C data to DS28E17. */
	crc = crc16(CRC16_INIT, command, command_count);
	w1_write_block(sl->master, command, command_count);

	w1_buf[0] = count;
	crc = crc16(crc, w1_buf, 1);
	w1_write_8(sl->master, w1_buf[0]);

	crc = crc16(crc, buffer, count);
	w1_write_block(sl->master, buffer, count);

	w1_buf[0] = ~(crc & 0xFF);
	w1_buf[1] = ~((crc >> 8) & 0xFF);
	w1_write_block(sl->master, w1_buf, 2);

	/* Wait until busy flag clears (or timeout). */
	if (w1_f19_i2c_busy_wait(sl, count + 1) < 0)
		return -ETIMEDOUT;

	/* Read status from DS28E17. */
	w1_read_block(sl->master, w1_buf, 2);

	/* Check error conditions. */
	error = w1_f19_error(sl, w1_buf);
	if (error < 0)
		return error;

	/* Return number of bytes written. */
	return count;
}


/* Write data to I2C slave. */
static int w1_f19_i2c_write(struct w1_slave *sl, u16 i2c_address,
	const u8 *buffer, size_t count, bool stop)
{
	int result;
	int remaining = count;
	const u8 *p;
	u8 command[2];

	/* Check input. */
	if (count == 0)
		return -EOPNOTSUPP;

	/* Check whether we need multiple commands. */
	if (count <= W1_F19_WRITE_DATA_LIMIT) {
		/*
		 * Small data amount. Data can be sent with
		 * a single onewire command.
		 */

		/* Send all data to DS28E17. */
		command[0] = (stop ? W1_F19_WRITE_DATA_WITH_STOP
			: W1_F19_WRITE_DATA_NO_STOP);
		command[1] = i2c_address << 1;
		result = __w1_f19_i2c_write(sl, command, 2, buffer, count);
	} else {
		/* Large data amount. Data has to be sent in multiple chunks. */

		/* Send first chunk to DS28E17. */
		p = buffer;
		command[0] = W1_F19_WRITE_DATA_NO_STOP;
		command[1] = i2c_address << 1;
		result = __w1_f19_i2c_write(sl, command, 2, p,
			W1_F19_WRITE_DATA_LIMIT);
		if (result < 0)
			return result;

		/* Resume to same DS28E17. */
		if (w1_reset_resume_command(sl->master))
			return -EIO;

		/* Next data chunk. */
		p += W1_F19_WRITE_DATA_LIMIT;
		remaining -= W1_F19_WRITE_DATA_LIMIT;

		while (remaining > W1_F19_WRITE_DATA_LIMIT) {
			/* Send intermediate chunk to DS28E17. */
			command[0] = W1_F19_WRITE_DATA_ONLY;
			result = __w1_f19_i2c_write(sl, command, 1, p,
					W1_F19_WRITE_DATA_LIMIT);
			if (result < 0)
				return result;

			/* Resume to same DS28E17. */
			if (w1_reset_resume_command(sl->master))
				return -EIO;

			/* Next data chunk. */
			p += W1_F19_WRITE_DATA_LIMIT;
			remaining -= W1_F19_WRITE_DATA_LIMIT;
		}

		/* Send final chunk to DS28E17. */
		command[0] = (stop ? W1_F19_WRITE_DATA_ONLY_WITH_STOP
			: W1_F19_WRITE_DATA_ONLY);
		result = __w1_f19_i2c_write(sl, command, 1, p, remaining);
	}

	return result;
}


/* Read data from I2C slave. */
static int w1_f19_i2c_read(struct w1_slave *sl, u16 i2c_address,
	u8 *buffer, size_t count)
{
	u16 crc;
	int error;
	u8 w1_buf[5];

	/* Check input. */
	if (count == 0)
		return -EOPNOTSUPP;

	/* Send command to DS28E17. */
	w1_buf[0] = W1_F19_READ_DATA_WITH_STOP;
	w1_buf[1] = i2c_address << 1 | 0x01;
	w1_buf[2] = count;
	crc = crc16(CRC16_INIT, w1_buf, 3);
	w1_buf[3] = ~(crc & 0xFF);
	w1_buf[4] = ~((crc >> 8) & 0xFF);
	w1_write_block(sl->master, w1_buf, 5);

	/* Wait until busy flag clears (or timeout). */
	if (w1_f19_i2c_busy_wait(sl, count + 1) < 0)
		return -ETIMEDOUT;

	/* Read status from DS28E17. */
	w1_buf[0] = w1_read_8(sl->master);
	w1_buf[1] = 0;

	/* Check error conditions. */
	error = w1_f19_error(sl, w1_buf);
	if (error < 0)
		return error;

	/* Read received I2C data from DS28E17. */
	return w1_read_block(sl->master, buffer, count);
}


/* Write to, then read data from I2C slave. */
static int w1_f19_i2c_write_read(struct w1_slave *sl, u16 i2c_address,
	const u8 *wbuffer, size_t wcount, u8 *rbuffer, size_t rcount)
{
	u16 crc;
	int error;
	u8 w1_buf[3];

	/* Check input. */
	if (wcount == 0 || rcount == 0)
		return -EOPNOTSUPP;

	/* Send command and I2C data to DS28E17. */
	w1_buf[0] = W1_F19_WRITE_READ_DATA_WITH_STOP;
	w1_buf[1] = i2c_address << 1;
	w1_buf[2] = wcount;
	crc = crc16(CRC16_INIT, w1_buf, 3);
	w1_write_block(sl->master, w1_buf, 3);

	crc = crc16(crc, wbuffer, wcount);
	w1_write_block(sl->master, wbuffer, wcount);

	w1_buf[0] = rcount;
	crc = crc16(crc, w1_buf, 1);
	w1_buf[1] = ~(crc & 0xFF);
	w1_buf[2] = ~((crc >> 8) & 0xFF);
	w1_write_block(sl->master, w1_buf, 3);

	/* Wait until busy flag clears (or timeout). */
	if (w1_f19_i2c_busy_wait(sl, wcount + rcount + 2) < 0)
		return -ETIMEDOUT;

	/* Read status from DS28E17. */
	w1_read_block(sl->master, w1_buf, 2);

	/* Check error conditions. */
	error = w1_f19_error(sl, w1_buf);
	if (error < 0)
		return error;

	/* Read received I2C data from DS28E17. */
	return w1_read_block(sl->master, rbuffer, rcount);
}


/* Do an I2C master transfer. */
static int w1_f19_i2c_master_transfer(struct i2c_adapter *adapter,
	struct i2c_msg *msgs, int num)
{
	struct w1_slave *sl = (struct w1_slave *) adapter->algo_data;
	int i = 0;
	int result = 0;

	/* Start onewire transaction. */
	mutex_lock(&sl->master->bus_mutex);

	/* Select DS28E17. */
	if (w1_reset_select_slave(sl)) {
		i = -EIO;
		goto error;
	}

	/* Loop while there are still messages to transfer. */
	while (i < num) {
		/*
		 * Check for special case: Small write followed
		 * by read to same I2C device.
		 */
		if (i < (num-1)
			&& msgs[i].addr == msgs[i+1].addr
			&& !(msgs[i].flags & I2C_M_RD)
			&& (msgs[i+1].flags & I2C_M_RD)
			&& (msgs[i].len <= W1_F19_WRITE_DATA_LIMIT)) {
			/*
			 * The DS28E17 has a combined transfer
			 * for small write+read.
			 */
			result = w1_f19_i2c_write_read(sl, msgs[i].addr,
				msgs[i].buf, msgs[i].len,
				msgs[i+1].buf, msgs[i+1].len);
			if (result < 0) {
				i = result;
				goto error;
			}

			/*
			 * Check if we should interpret the read data
			 * as a length byte. The DS28E17 unfortunately
			 * has no read without stop, so we can just do
			 * another simple read in that case.
			 */
			if (msgs[i+1].flags & I2C_M_RECV_LEN) {
				result = w1_f19_i2c_read(sl, msgs[i+1].addr,
					&(msgs[i+1].buf[1]), msgs[i+1].buf[0]);
				if (result < 0) {
					i = result;
					goto error;
				}
			}

			/* Eat up read message, too. */
			i++;
		} else if (msgs[i].flags & I2C_M_RD) {
			/* Read transfer. */
			result = w1_f19_i2c_read(sl, msgs[i].addr,
				msgs[i].buf, msgs[i].len);
			if (result < 0) {
				i = result;
				goto error;
			}

			/*
			 * Check if we should interpret the read data
			 * as a length byte. The DS28E17 unfortunately
			 * has no read without stop, so we can just do
			 * another simple read in that case.
			 */
			if (msgs[i].flags & I2C_M_RECV_LEN) {
				result = w1_f19_i2c_read(sl,
					msgs[i].addr,
					&(msgs[i].buf[1]),
					msgs[i].buf[0]);
				if (result < 0) {
					i = result;
					goto error;
				}
			}
		} else {
			/*
			 * Write transfer.
			 * Stop condition only for last
			 * transfer.
			 */
			result = w1_f19_i2c_write(sl,
				msgs[i].addr,
				msgs[i].buf,
				msgs[i].len,
				i == (num-1));
			if (result < 0) {
				i = result;
				goto error;
			}
		}

		/* Next message. */
		i++;

		/* Are there still messages to send/receive? */
		if (i < num) {
			/* Yes. Resume to same DS28E17. */
			if (w1_reset_resume_command(sl->master)) {
				i = -EIO;
				goto error;
			}
		}
	}

error:
	/* End onewire transaction. */
	mutex_unlock(&sl->master->bus_mutex);

	/* Return number of messages processed or error. */
	return i;
}


/* Get I2C adapter functionality. */
static u32 w1_f19_i2c_functionality(struct i2c_adapter *adapter)
{
	/*
	 * Plain I2C functions only.
	 * SMBus is emulated by the kernel's I2C layer.
	 * No "I2C_FUNC_SMBUS_QUICK"
	 * No "I2C_FUNC_SMBUS_READ_BLOCK_DATA"
	 * No "I2C_FUNC_SMBUS_BLOCK_PROC_CALL"
	 */
	return I2C_FUNC_I2C |
		I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_PROC_CALL |
		I2C_FUNC_SMBUS_WRITE_BLOCK_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK |
		I2C_FUNC_SMBUS_PEC;
}


/* I2C adapter quirks. */
static const struct i2c_adapter_quirks w1_f19_i2c_adapter_quirks = {
	.max_read_len = W1_F19_READ_DATA_LIMIT,
};

/* I2C algorithm. */
static const struct i2c_algorithm w1_f19_i2c_algorithm = {
	.master_xfer    = w1_f19_i2c_master_transfer,
	.functionality  = w1_f19_i2c_functionality,
};


/* Read I2C speed from DS28E17. */
static int w1_f19_get_i2c_speed(struct w1_slave *sl)
{
	struct w1_f19_data *data = sl->family_data;
	int result = -EIO;

	/* Start onewire transaction. */
	mutex_lock(&sl->master->bus_mutex);

	/* Select slave. */
	if (w1_reset_select_slave(sl))
		goto error;

	/* Read slave configuration byte. */
	w1_write_8(sl->master, W1_F19_READ_CONFIGURATION);
	result = w1_read_8(sl->master);
	if (result < 0 || result > 2) {
		result = -EIO;
		goto error;
	}

	/* Update speed in slave specific data. */
	data->speed = result;

error:
	/* End onewire transaction. */
	mutex_unlock(&sl->master->bus_mutex);

	return result;
}


/* Set I2C speed on DS28E17. */
static int __w1_f19_set_i2c_speed(struct w1_slave *sl, u8 speed)
{
	struct w1_f19_data *data = sl->family_data;
	const int i2c_speeds[3] = { 100, 400, 900 };
	u8 w1_buf[2];

	/* Select slave. */
	if (w1_reset_select_slave(sl))
		return -EIO;

	w1_buf[0] = W1_F19_WRITE_CONFIGURATION;
	w1_buf[1] = speed;
	w1_write_block(sl->master, w1_buf, 2);

	/* Update speed in slave specific data. */
	data->speed = speed;

	dev_info(&sl->dev, "i2c speed set to %d kBaud\n", i2c_speeds[speed]);

	return 0;
}

static int w1_f19_set_i2c_speed(struct w1_slave *sl, u8 speed)
{
	int result;

	/* Start onewire transaction. */
	mutex_lock(&sl->master->bus_mutex);

	/* Set I2C speed on DS28E17. */
	result = __w1_f19_set_i2c_speed(sl, speed);

	/* End onewire transaction. */
	mutex_unlock(&sl->master->bus_mutex);

	return result;
}


/* Sysfs attributes. */

/* I2C speed attribute for a single chip. */
static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(dev);
	int result;

	/* Read current speed from slave. Updates data->speed. */
	result = w1_f19_get_i2c_speed(sl);
	if (result < 0)
		return result;

	/* Return current speed value. */
	return sprintf(buf, "%d\n", result);
}

static ssize_t speed_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct w1_slave *sl = dev_to_w1_slave(dev);
	int error;

	/* Valid values are: "100", "400", "900" */
	if (count < 3 || count > 4 || !buf)
		return -EINVAL;
	if (count == 4 && buf[3] != '\n')
		return -EINVAL;
	if (buf[1] != '0' || buf[2] != '0')
		return -EINVAL;

	/* Set speed on slave. */
	switch (buf[0]) {
	case '1':
		error = w1_f19_set_i2c_speed(sl, 0);
		break;
	case '4':
		error = w1_f19_set_i2c_speed(sl, 1);
		break;
	case '9':
		error = w1_f19_set_i2c_speed(sl, 2);
		break;
	default:
		return -EINVAL;
	}

	if (error < 0)
		return error;

	/* Return bytes written. */
	return count;
}

static DEVICE_ATTR_RW(speed);


/* Busy stretch attribute for a single chip. */
static ssize_t stretch_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(dev);
	struct w1_f19_data *data = sl->family_data;

	/* Return current stretch value. */
	return sprintf(buf, "%d\n", data->stretch);
}

static ssize_t stretch_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct w1_slave *sl = dev_to_w1_slave(dev);
	struct w1_f19_data *data = sl->family_data;

	/* Valid values are '1' to '9' */
	if (count < 1 || count > 2 || !buf)
		return -EINVAL;
	if (count == 2 && buf[1] != '\n')
		return -EINVAL;
	if (buf[0] < '1' || buf[0] > '9')
		return -EINVAL;

	/* Set busy stretch value. */
	data->stretch = buf[0] & 0x0F;

	/* Return bytes written. */
	return count;
}

static DEVICE_ATTR_RW(stretch);


/* All attributes. */
static struct attribute *w1_f19_attrs[] = {
	&dev_attr_speed.attr,
	&dev_attr_stretch.attr,
	NULL,
};

static const struct attribute_group w1_f19_group = {
	.attrs		= w1_f19_attrs,
};

static const struct attribute_group *w1_f19_groups[] = {
	&w1_f19_group,
	NULL,
};


/* Slave add and remove functions. */
static int w1_f19_add_slave(struct w1_slave *sl)
{
	struct w1_f19_data *data = NULL;

	/* Allocate memory for slave specific data. */
	data = devm_kzalloc(&sl->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	sl->family_data = data;

	/* Setup default I2C speed on slave. */
	switch (i2c_speed) {
	case 100:
		__w1_f19_set_i2c_speed(sl, 0);
		break;
	case 400:
		__w1_f19_set_i2c_speed(sl, 1);
		break;
	case 900:
		__w1_f19_set_i2c_speed(sl, 2);
		break;
	default:
		/*
		 * A i2c_speed module parameter of anything else
		 * than 100, 400, 900 means not to touch the
		 * speed of the DS28E17.
		 * We assume 400kBaud, the power-on value.
		 */
		data->speed = 1;
	}

	/*
	 * Setup default busy stretch
	 * configuration for the DS28E17.
	 */
	data->stretch = i2c_stretch;

	/* Setup I2C adapter. */
	data->adapter.owner      = THIS_MODULE;
	data->adapter.algo       = &w1_f19_i2c_algorithm;
	data->adapter.algo_data  = sl;
	strcpy(data->adapter.name, "w1-");
	strcat(data->adapter.name, sl->name);
	data->adapter.dev.parent = &sl->dev;
	data->adapter.quirks     = &w1_f19_i2c_adapter_quirks;

	return i2c_add_adapter(&data->adapter);
}

static void w1_f19_remove_slave(struct w1_slave *sl)
{
	struct w1_f19_data *family_data = sl->family_data;

	/* Delete I2C adapter. */
	i2c_del_adapter(&family_data->adapter);

	/* Free slave specific data. */
	devm_kfree(&sl->dev, family_data);
	sl->family_data = NULL;
}


/* Declarations within the w1 subsystem. */
static const struct w1_family_ops w1_f19_fops = {
	.add_slave = w1_f19_add_slave,
	.remove_slave = w1_f19_remove_slave,
	.groups = w1_f19_groups,
};

static struct w1_family w1_family_19 = {
	.fid = W1_FAMILY_DS28E17,
	.fops = &w1_f19_fops,
};


/* Module init and remove functions. */
static int __init w1_f19_init(void)
{
	return w1_register_family(&w1_family_19);
}

static void __exit w1_f19_fini(void)
{
	w1_unregister_family(&w1_family_19);
}

module_init(w1_f19_init);
module_exit(w1_f19_fini);

