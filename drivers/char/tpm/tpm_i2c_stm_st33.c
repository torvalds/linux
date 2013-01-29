/*
 * STMicroelectronics TPM I2C Linux driver for TPM ST33ZP24
 * Copyright (C) 2009, 2010  STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * STMicroelectronics version 1.2.0, Copyright (C) 2010
 * STMicroelectronics comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions.
 *
 * @Author: Christophe RICARD tpmsupport@st.com
 *
 * @File: tpm_stm_st33_i2c.c
 *
 * @Synopsis:
 *	09/15/2010:	First shot driver tpm_tis driver for
			 lpc is used as model.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "tpm.h"
#include "tpm_i2c_stm_st33.h"

enum stm33zp24_access {
	TPM_ACCESS_VALID = 0x80,
	TPM_ACCESS_ACTIVE_LOCALITY = 0x20,
	TPM_ACCESS_REQUEST_PENDING = 0x04,
	TPM_ACCESS_REQUEST_USE = 0x02,
};

enum stm33zp24_status {
	TPM_STS_VALID = 0x80,
	TPM_STS_COMMAND_READY = 0x40,
	TPM_STS_GO = 0x20,
	TPM_STS_DATA_AVAIL = 0x10,
	TPM_STS_DATA_EXPECT = 0x08,
};

enum stm33zp24_int_flags {
	TPM_GLOBAL_INT_ENABLE = 0x80,
	TPM_INTF_CMD_READY_INT = 0x080,
	TPM_INTF_FIFO_AVALAIBLE_INT = 0x040,
	TPM_INTF_WAKE_UP_READY_INT = 0x020,
	TPM_INTF_LOCALITY_CHANGE_INT = 0x004,
	TPM_INTF_STS_VALID_INT = 0x002,
	TPM_INTF_DATA_AVAIL_INT = 0x001,
};

enum tis_defaults {
	TIS_SHORT_TIMEOUT = 750,
	TIS_LONG_TIMEOUT = 2000,
};

/*
 * write8_reg
 * Send byte to the TIS register according to the ST33ZP24 I2C protocol.
 * @param: tpm_register, the tpm tis register where the data should be written
 * @param: tpm_data, the tpm_data to write inside the tpm_register
 * @param: tpm_size, The length of the data
 * @return: Returns negative errno, or else the number of bytes written.
 */
static int write8_reg(struct i2c_client *client, u8 tpm_register,
		      u8 *tpm_data, u16 tpm_size)
{
	int value = 0;
	struct st33zp24_platform_data *pin_infos;

	pin_infos = client->dev.platform_data;

	pin_infos->tpm_i2c_buffer[0][0] = tpm_register;
	memcpy(&pin_infos->tpm_i2c_buffer[0][1], tpm_data, tpm_size);
	value = i2c_master_send(client, pin_infos->tpm_i2c_buffer[0],
				tpm_size + 1);
	return value;
} /* write8_reg() */

/*
 * read8_reg
 * Recv byte from the TIS register according to the ST33ZP24 I2C protocol.
 * @param: tpm_register, the tpm tis register where the data should be read
 * @param: tpm_data, the TPM response
 * @param: tpm_size, tpm TPM response size to read.
 * @return: number of byte read successfully: should be one if success.
 */
static int read8_reg(struct i2c_client *client, u8 tpm_register,
		    u8 *tpm_data, int tpm_size)
{
	u8 status = 0;
	u8 data;

	data = TPM_DUMMY_BYTE;
	status = write8_reg(client, tpm_register, &data, 1);
	if (status == 2)
		status = i2c_master_recv(client, tpm_data, tpm_size);
	return status;
} /* read8_reg() */

/*
 * I2C_WRITE_DATA
 * Send byte to the TIS register according to the ST33ZP24 I2C protocol.
 * @param: client, the chip description
 * @param: tpm_register, the tpm tis register where the data should be written
 * @param: tpm_data, the tpm_data to write inside the tpm_register
 * @param: tpm_size, The length of the data
 * @return: number of byte written successfully: should be one if success.
 */
#define I2C_WRITE_DATA(client, tpm_register, tpm_data, tpm_size) \
	(write8_reg(client, tpm_register | \
	TPM_WRITE_DIRECTION, tpm_data, tpm_size))

/*
 * I2C_READ_DATA
 * Recv byte from the TIS register according to the ST33ZP24 I2C protocol.
 * @param: tpm, the chip description
 * @param: tpm_register, the tpm tis register where the data should be read
 * @param: tpm_data, the TPM response
 * @param: tpm_size, tpm TPM response size to read.
 * @return: number of byte read successfully: should be one if success.
 */
#define I2C_READ_DATA(client, tpm_register, tpm_data, tpm_size) \
	(read8_reg(client, tpm_register, tpm_data, tpm_size))

/*
 * clear_interruption
 * clear the TPM interrupt register.
 * @param: tpm, the chip description
 */
static void clear_interruption(struct i2c_client *client)
{
	u8 interrupt;
	I2C_READ_DATA(client, TPM_INT_STATUS, &interrupt, 1);
	I2C_WRITE_DATA(client, TPM_INT_STATUS, &interrupt, 1);
	I2C_READ_DATA(client, TPM_INT_STATUS, &interrupt, 1);
} /* clear_interruption() */

/*
 * _wait_for_interrupt_serirq_timeout
 * @param: tpm, the chip description
 * @param: timeout, the timeout of the interrupt
 * @return: the status of the interruption.
 */
static long _wait_for_interrupt_serirq_timeout(struct tpm_chip *chip,
						unsigned long timeout)
{
	long status;
	struct i2c_client *client;
	struct st33zp24_platform_data *pin_infos;

	client = (struct i2c_client *) TPM_VPRIV(chip);
	pin_infos = client->dev.platform_data;

	status = wait_for_completion_interruptible_timeout(
					&pin_infos->irq_detection,
						timeout);
	if (status > 0)
		enable_irq(gpio_to_irq(pin_infos->io_serirq));
	gpio_direction_input(pin_infos->io_serirq);

	return status;
} /* wait_for_interrupt_serirq_timeout() */

static int wait_for_serirq_timeout(struct tpm_chip *chip, bool condition,
				 unsigned long timeout)
{
	int status = 2;
	struct i2c_client *client;

	client = (struct i2c_client *) TPM_VPRIV(chip);

	status = _wait_for_interrupt_serirq_timeout(chip, timeout);
	if (!status) {
		status = -EBUSY;
	} else{
		clear_interruption(client);
		if (condition)
			status = 1;
	}
	return status;
}

/*
 * tpm_stm_i2c_cancel, cancel is not implemented.
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h
 */
static void tpm_stm_i2c_cancel(struct tpm_chip *chip)
{
	struct i2c_client *client;
	u8 data;

	client = (struct i2c_client *) TPM_VPRIV(chip);

	data = TPM_STS_COMMAND_READY;
	I2C_WRITE_DATA(client, TPM_STS, &data, 1);
	if (chip->vendor.irq)
		wait_for_serirq_timeout(chip, 1, chip->vendor.timeout_a);
}	/* tpm_stm_i2c_cancel() */

/*
 * tpm_stm_spi_status return the TPM_STS register
 * @param: chip, the tpm chip description
 * @return: the TPM_STS register value.
 */
static u8 tpm_stm_i2c_status(struct tpm_chip *chip)
{
	struct i2c_client *client;
	u8 data;
	client = (struct i2c_client *) TPM_VPRIV(chip);

	I2C_READ_DATA(client, TPM_STS, &data, 1);
	return data;
}				/* tpm_stm_i2c_status() */


/*
 * check_locality if the locality is active
 * @param: chip, the tpm chip description
 * @return: the active locality or -EACCESS.
 */
static int check_locality(struct tpm_chip *chip)
{
	struct i2c_client *client;
	u8 data;
	u8 status;

	client = (struct i2c_client *) TPM_VPRIV(chip);

	status = I2C_READ_DATA(client, TPM_ACCESS, &data, 1);
	if (status && (data &
		(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) ==
		(TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID))
		return chip->vendor.locality;

	return -EACCES;

} /* check_locality() */

/*
 * request_locality request the TPM locality
 * @param: chip, the chip description
 * @return: the active locality or EACCESS.
 */
static int request_locality(struct tpm_chip *chip)
{
	unsigned long stop;
	long rc;
	struct i2c_client *client;
	u8 data;

	client = (struct i2c_client *) TPM_VPRIV(chip);

	if (check_locality(chip) == chip->vendor.locality)
		return chip->vendor.locality;

	data = TPM_ACCESS_REQUEST_USE;
	rc = I2C_WRITE_DATA(client, TPM_ACCESS, &data, 1);
	if (rc < 0)
		goto end;

	if (chip->vendor.irq) {
		rc = wait_for_serirq_timeout(chip, (check_locality
						       (chip) >= 0),
						      chip->vendor.timeout_a);
		if (rc > 0)
			return chip->vendor.locality;
	} else{
		stop = jiffies + chip->vendor.timeout_a;
		do {
			if (check_locality(chip) >= 0)
				return chip->vendor.locality;
			msleep(TPM_TIMEOUT);
		} while (time_before(jiffies, stop));
	}
	rc = -EACCES;
end:
	return rc;
} /* request_locality() */

/*
 * release_locality release the active locality
 * @param: chip, the tpm chip description.
 */
static void release_locality(struct tpm_chip *chip)
{
	struct i2c_client *client;
	u8 data;

	client = (struct i2c_client *) TPM_VPRIV(chip);
	data = TPM_ACCESS_ACTIVE_LOCALITY;

	I2C_WRITE_DATA(client, TPM_ACCESS, &data, 1);
}

/*
 * get_burstcount return the burstcount address 0x19 0x1A
 * @param: chip, the chip description
 * return: the burstcount.
 */
static int get_burstcount(struct tpm_chip *chip)
{
	unsigned long stop;
	int burstcnt, status;
	u8 tpm_reg, temp;

	struct i2c_client *client = (struct i2c_client *) TPM_VPRIV(chip);

	stop = jiffies + chip->vendor.timeout_d;
	do {
		tpm_reg = TPM_STS + 1;
		status = I2C_READ_DATA(client, tpm_reg, &temp, 1);
		if (status < 0)
			goto end;

		tpm_reg = tpm_reg + 1;
		burstcnt = temp;
		status = I2C_READ_DATA(client, tpm_reg, &temp, 1);
		if (status < 0)
			goto end;

		burstcnt |= temp << 8;
		if (burstcnt)
			return burstcnt;
		msleep(TPM_TIMEOUT);
	} while (time_before(jiffies, stop));

end:
	return -EBUSY;
} /* get_burstcount() */

/*
 * wait_for_stat wait for a TPM_STS value
 * @param: chip, the tpm chip description
 * @param: mask, the value mask to wait
 * @param: timeout, the timeout
 * @param: queue, the wait queue.
 * @return: the tpm status, 0 if success, -ETIME if timeout is reached.
 */
static int wait_for_stat(struct tpm_chip *chip, u8 mask, unsigned long timeout,
			 wait_queue_head_t *queue)
{
	unsigned long stop;
	long rc;
	u8 status;

	 if (chip->vendor.irq) {
		rc = wait_for_serirq_timeout(chip, ((tpm_stm_i2c_status
							(chip) & mask) ==
						       mask), timeout);
		if (rc > 0)
			return 0;
	} else{
		stop = jiffies + timeout;
		do {
			msleep(TPM_TIMEOUT);
			status = tpm_stm_i2c_status(chip);
			if ((status & mask) == mask)
				return 0;
		} while (time_before(jiffies, stop));
	}
	return -ETIME;
} /* wait_for_stat() */

/*
 * recv_data receive data
 * @param: chip, the tpm chip description
 * @param: buf, the buffer where the data are received
 * @param: count, the number of data to receive
 * @return: the number of bytes read from TPM FIFO.
 */
static int recv_data(struct tpm_chip *chip, u8 *buf, size_t count)
{
	int size = 0, burstcnt, len;
	struct i2c_client *client;

	client = (struct i2c_client *) TPM_VPRIV(chip);

	while (size < count &&
	       wait_for_stat(chip,
			     TPM_STS_DATA_AVAIL | TPM_STS_VALID,
			     chip->vendor.timeout_c,
			     &chip->vendor.read_queue)
	       == 0) {
		burstcnt = get_burstcount(chip);
		len = min_t(int, burstcnt, count - size);
		I2C_READ_DATA(client, TPM_DATA_FIFO, buf + size, len);
		size += len;
	}
	return size;
}

/*
 * tpm_ioserirq_handler the serirq irq handler
 * @param: irq, the tpm chip description
 * @param: dev_id, the description of the chip
 * @return: the status of the handler.
 */
static irqreturn_t tpm_ioserirq_handler(int irq, void *dev_id)
{
	struct tpm_chip *chip = dev_id;
	struct i2c_client *client;
	struct st33zp24_platform_data *pin_infos;

	disable_irq_nosync(irq);

	client = (struct i2c_client *) TPM_VPRIV(chip);
	pin_infos = client->dev.platform_data;

	complete(&pin_infos->irq_detection);
	return IRQ_HANDLED;
} /* tpm_ioserirq_handler() */


/*
 * tpm_stm_i2c_send send TPM commands through the I2C bus.
 *
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h
 * @param: buf,	the buffer to send.
 * @param: count, the number of bytes to send.
 * @return: In case of success the number of bytes sent.
 *			In other case, a < 0 value describing the issue.
 */
static int tpm_stm_i2c_send(struct tpm_chip *chip, unsigned char *buf,
			    size_t len)
{
	u32 status,
	    burstcnt = 0, i, size;
	int ret;
	u8 data;
	struct i2c_client *client;

	if (chip == NULL)
		return -EBUSY;
	if (len < TPM_HEADER_SIZE)
		return -EBUSY;

	client = (struct i2c_client *)TPM_VPRIV(chip);

	client->flags = 0;

	ret = request_locality(chip);
	if (ret < 0)
		return ret;

	status = tpm_stm_i2c_status(chip);
	if ((status & TPM_STS_COMMAND_READY) == 0) {
		tpm_stm_i2c_cancel(chip);
		if (wait_for_stat
		    (chip, TPM_STS_COMMAND_READY, chip->vendor.timeout_b,
		     &chip->vendor.int_queue) < 0) {
			ret = -ETIME;
			goto out_err;
		}
	}

	for (i = 0 ; i < len - 1 ;) {
		burstcnt = get_burstcount(chip);
		size = min_t(int, len - i - 1, burstcnt);
		ret = I2C_WRITE_DATA(client, TPM_DATA_FIFO, buf, size);
		if (ret < 0)
			goto out_err;

		i += size;
	}

	status = tpm_stm_i2c_status(chip);
	if ((status & TPM_STS_DATA_EXPECT) == 0) {
		ret = -EIO;
		goto out_err;
	}

	ret = I2C_WRITE_DATA(client, TPM_DATA_FIFO, buf + len - 1, 1);
	if (ret < 0)
		goto out_err;

	status = tpm_stm_i2c_status(chip);
	if ((status & TPM_STS_DATA_EXPECT) != 0) {
		ret = -EIO;
		goto out_err;
	}

	data = TPM_STS_GO;
	I2C_WRITE_DATA(client, TPM_STS, &data, 1);

	return len;
out_err:
	tpm_stm_i2c_cancel(chip);
	release_locality(chip);
	return ret;
}

/*
 * tpm_stm_i2c_recv received TPM response through the I2C bus.
 * @param: chip, the tpm_chip description as specified in driver/char/tpm/tpm.h.
 * @param: buf,	the buffer to store datas.
 * @param: count, the number of bytes to send.
 * @return: In case of success the number of bytes received.
 *		In other case, a < 0 value describing the issue.
 */
static int tpm_stm_i2c_recv(struct tpm_chip *chip, unsigned char *buf,
			    size_t count)
{
	int size = 0;
	int expected;

	if (chip == NULL)
		return -EBUSY;

	if (count < TPM_HEADER_SIZE) {
		size = -EIO;
		goto out;
	}

	size = recv_data(chip, buf, TPM_HEADER_SIZE);
	if (size < TPM_HEADER_SIZE) {
		dev_err(chip->dev, "Unable to read header\n");
		goto out;
	}

	expected = be32_to_cpu(*(__be32 *) (buf + 2));
	if (expected > count) {
		size = -EIO;
		goto out;
	}

	size += recv_data(chip, &buf[TPM_HEADER_SIZE],
					expected - TPM_HEADER_SIZE);
	if (size < expected) {
		dev_err(chip->dev, "Unable to read remainder of result\n");
		size = -ETIME;
		goto out;
	}

out:
	chip->vendor.cancel(chip);
	release_locality(chip);
	return size;
}

static bool tpm_st33_i2c_req_canceled(struct tpm_chip *chip, u8 status)
{
       return (status == TPM_STS_COMMAND_READY);
}

static const struct file_operations tpm_st33_i2c_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = tpm_read,
	.write = tpm_write,
	.open = tpm_open,
	.release = tpm_release,
};

static DEVICE_ATTR(pubek, S_IRUGO, tpm_show_pubek, NULL);
static DEVICE_ATTR(pcrs, S_IRUGO, tpm_show_pcrs, NULL);
static DEVICE_ATTR(enabled, S_IRUGO, tpm_show_enabled, NULL);
static DEVICE_ATTR(active, S_IRUGO, tpm_show_active, NULL);
static DEVICE_ATTR(owned, S_IRUGO, tpm_show_owned, NULL);
static DEVICE_ATTR(temp_deactivated, S_IRUGO, tpm_show_temp_deactivated, NULL);
static DEVICE_ATTR(caps, S_IRUGO, tpm_show_caps_1_2, NULL);
static DEVICE_ATTR(cancel, S_IWUSR | S_IWGRP, NULL, tpm_store_cancel);

static struct attribute *stm_tpm_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_enabled.attr,
	&dev_attr_active.attr,
	&dev_attr_owned.attr,
	&dev_attr_temp_deactivated.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr, NULL,
};

static struct attribute_group stm_tpm_attr_grp = {
	.attrs = stm_tpm_attrs
};

static struct tpm_vendor_specific st_i2c_tpm = {
	.send = tpm_stm_i2c_send,
	.recv = tpm_stm_i2c_recv,
	.cancel = tpm_stm_i2c_cancel,
	.status = tpm_stm_i2c_status,
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = tpm_st33_i2c_req_canceled,
	.attr_group = &stm_tpm_attr_grp,
	.miscdev = {.fops = &tpm_st33_i2c_fops,},
};

static int interrupts ;
module_param(interrupts, int, 0444);
MODULE_PARM_DESC(interrupts, "Enable interrupts");

static int power_mgt = 1;
module_param(power_mgt, int, 0444);
MODULE_PARM_DESC(power_mgt, "Power Management");

/*
 * tpm_st33_i2c_probe initialize the TPM device
 * @param: client, the i2c_client drescription (TPM I2C description).
 * @param: id, the i2c_device_id struct.
 * @return: 0 in case of success.
 *	 -1 in other case.
 */
static int
tpm_st33_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err;
	u8 intmask;
	struct tpm_chip *chip;
	struct st33zp24_platform_data *platform_data;

	if (client == NULL) {
		pr_info("%s: i2c client is NULL. Device not accessible.\n",
			__func__);
		err = -ENODEV;
		goto end;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_info(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto end;
	}

	chip = tpm_register_hardware(&client->dev, &st_i2c_tpm);
	if (!chip) {
		dev_info(&client->dev, "fail chip\n");
		err = -ENODEV;
		goto end;
	}

	platform_data = client->dev.platform_data;

	if (!platform_data) {
		dev_info(&client->dev, "chip not available\n");
		err = -ENODEV;
		goto _tpm_clean_answer;
	}

	platform_data->tpm_i2c_buffer[0] =
	    kmalloc(TPM_BUFSIZE * sizeof(u8), GFP_KERNEL);
	if (platform_data->tpm_i2c_buffer[0] == NULL) {
		err = -ENOMEM;
		goto _tpm_clean_answer;
	}
	platform_data->tpm_i2c_buffer[1] =
	    kmalloc(TPM_BUFSIZE * sizeof(u8), GFP_KERNEL);
	if (platform_data->tpm_i2c_buffer[1] == NULL) {
		err = -ENOMEM;
		goto _tpm_clean_response1;
	}

	TPM_VPRIV(chip) = client;

	chip->vendor.timeout_a = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->vendor.timeout_b = msecs_to_jiffies(TIS_LONG_TIMEOUT);
	chip->vendor.timeout_c = msecs_to_jiffies(TIS_SHORT_TIMEOUT);
	chip->vendor.timeout_d = msecs_to_jiffies(TIS_SHORT_TIMEOUT);

	chip->vendor.locality = LOCALITY0;

	if (power_mgt) {
		err = gpio_request(platform_data->io_lpcpd, "TPM IO_LPCPD");
		if (err)
			goto _gpio_init1;
		gpio_set_value(platform_data->io_lpcpd, 1);
	}

	if (interrupts) {
		init_completion(&platform_data->irq_detection);
		if (request_locality(chip) != LOCALITY0) {
			err = -ENODEV;
			goto _tpm_clean_response2;
		}
		err = gpio_request(platform_data->io_serirq, "TPM IO_SERIRQ");
		if (err)
			goto _gpio_init2;

		clear_interruption(client);
		err = request_irq(gpio_to_irq(platform_data->io_serirq),
				&tpm_ioserirq_handler,
				IRQF_TRIGGER_HIGH,
				"TPM SERIRQ management", chip);
		if (err < 0) {
			dev_err(chip->dev , "TPM SERIRQ signals %d not available\n",
			gpio_to_irq(platform_data->io_serirq));
			goto _irq_set;
		}

		err = I2C_READ_DATA(client, TPM_INT_ENABLE, &intmask, 1);
		if (err < 0)
			goto _irq_set;

		intmask |= TPM_INTF_CMD_READY_INT
			|  TPM_INTF_FIFO_AVALAIBLE_INT
			|  TPM_INTF_WAKE_UP_READY_INT
			|  TPM_INTF_LOCALITY_CHANGE_INT
			|  TPM_INTF_STS_VALID_INT
			|  TPM_INTF_DATA_AVAIL_INT;

		err = I2C_WRITE_DATA(client, TPM_INT_ENABLE, &intmask, 1);
		if (err < 0)
			goto _irq_set;

		intmask = TPM_GLOBAL_INT_ENABLE;
		err = I2C_WRITE_DATA(client, (TPM_INT_ENABLE + 3), &intmask, 1);
		if (err < 0)
			goto _irq_set;

		err = I2C_READ_DATA(client, TPM_INT_STATUS, &intmask, 1);
		if (err < 0)
			goto _irq_set;

		chip->vendor.irq = interrupts;

		tpm_gen_interrupt(chip);
	}

	tpm_get_timeouts(chip);

	i2c_set_clientdata(client, chip);

	dev_info(chip->dev, "TPM I2C Initialized\n");
	return 0;
_irq_set:
	free_irq(gpio_to_irq(platform_data->io_serirq), (void *) chip);
_gpio_init2:
	if (interrupts)
		gpio_free(platform_data->io_serirq);
_gpio_init1:
	if (power_mgt)
		gpio_free(platform_data->io_lpcpd);
_tpm_clean_response2:
	kzfree(platform_data->tpm_i2c_buffer[1]);
	platform_data->tpm_i2c_buffer[1] = NULL;
_tpm_clean_response1:
	kzfree(platform_data->tpm_i2c_buffer[0]);
	platform_data->tpm_i2c_buffer[0] = NULL;
_tpm_clean_answer:
	tpm_remove_hardware(chip->dev);
end:
	pr_info("TPM I2C initialisation fail\n");
	return err;
}

/*
 * tpm_st33_i2c_remove remove the TPM device
 * @param: client, the i2c_client drescription (TPM I2C description).
		clear_bit(0, &chip->is_open);
 * @return: 0 in case of success.
 */
static int tpm_st33_i2c_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = (struct tpm_chip *)i2c_get_clientdata(client);
	struct st33zp24_platform_data *pin_infos =
		((struct i2c_client *) TPM_VPRIV(chip))->dev.platform_data;

	if (pin_infos != NULL) {
		free_irq(pin_infos->io_serirq, chip);

		gpio_free(pin_infos->io_serirq);
		gpio_free(pin_infos->io_lpcpd);

		tpm_remove_hardware(chip->dev);

		if (pin_infos->tpm_i2c_buffer[1] != NULL) {
			kzfree(pin_infos->tpm_i2c_buffer[1]);
			pin_infos->tpm_i2c_buffer[1] = NULL;
		}
		if (pin_infos->tpm_i2c_buffer[0] != NULL) {
			kzfree(pin_infos->tpm_i2c_buffer[0]);
			pin_infos->tpm_i2c_buffer[0] = NULL;
		}
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * tpm_st33_i2c_pm_suspend suspend the TPM device
 * Added: Work around when suspend and no tpm application is running, suspend
 * may fail because chip->data_buffer is not set (only set in tpm_open in Linux
 * TPM core)
 * @param: client, the i2c_client drescription (TPM I2C description).
 * @param: mesg, the power management message.
 * @return: 0 in case of success.
 */
static int tpm_st33_i2c_pm_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct st33zp24_platform_data *pin_infos = dev->platform_data;
	int ret = 0;

	if (power_mgt)
		gpio_set_value(pin_infos->io_lpcpd, 0);
	else{
		if (chip->data_buffer == NULL)
			chip->data_buffer = pin_infos->tpm_i2c_buffer[0];
		ret = tpm_pm_suspend(dev);
	}
	return ret;
}				/* tpm_st33_i2c_suspend() */

/*
 * tpm_st33_i2c_pm_resume resume the TPM device
 * @param: client, the i2c_client drescription (TPM I2C description).
 * @return: 0 in case of success.
 */
static int tpm_st33_i2c_pm_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct st33zp24_platform_data *pin_infos = dev->platform_data;

	int ret = 0;

	if (power_mgt) {
		gpio_set_value(pin_infos->io_lpcpd, 1);
		ret = wait_for_serirq_timeout(chip,
					  (chip->vendor.status(chip) &
					  TPM_STS_VALID) == TPM_STS_VALID,
					  chip->vendor.timeout_b);
	} else{
	if (chip->data_buffer == NULL)
		chip->data_buffer = pin_infos->tpm_i2c_buffer[0];
	ret = tpm_pm_resume(dev);
	if (!ret)
		tpm_do_selftest(chip);
	}
	return ret;
}				/* tpm_st33_i2c_pm_resume() */
#endif

static const struct i2c_device_id tpm_st33_i2c_id[] = {
	{TPM_ST33_I2C, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tpm_st33_i2c_id);
static SIMPLE_DEV_PM_OPS(tpm_st33_i2c_ops, tpm_st33_i2c_pm_suspend, tpm_st33_i2c_pm_resume);
static struct i2c_driver tpm_st33_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = TPM_ST33_I2C,
		   .pm = &tpm_st33_i2c_ops,
		   },
	.probe = tpm_st33_i2c_probe,
	.remove = tpm_st33_i2c_remove,
	.id_table = tpm_st33_i2c_id
};

module_i2c_driver(tpm_st33_i2c_driver);

MODULE_AUTHOR("Christophe Ricard (tpmsupport@st.com)");
MODULE_DESCRIPTION("STM TPM I2C ST33 Driver");
MODULE_VERSION("1.2.0");
MODULE_LICENSE("GPL");
