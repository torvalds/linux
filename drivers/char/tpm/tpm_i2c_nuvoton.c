/******************************************************************************
 * Nuvoton TPM I2C Device Driver Interface for WPCT301/NPCT501,
 * based on the TCG TPM Interface Spec version 1.2.
 * Specifications at www.trustedcomputinggroup.org
 *
 * Copyright (C) 2011, Nuvoton Technology Corporation.
 *  Dan Morav <dan.morav@nuvoton.com>
 * Copyright (C) 2013, Obsidian Research Corp.
 *  Jason Gunthorpe <jgunthorpe@obsidianresearch.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/>.
 *
 * Nuvoton contact information: APC.Support@nuvoton.com
 *****************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include "tpm.h"

/* I2C interface offsets */
#define TPM_STS                0x00
#define TPM_BURST_COUNT        0x01
#define TPM_DATA_FIFO_W        0x20
#define TPM_DATA_FIFO_R        0x40
#define TPM_VID_DID_RID        0x60
/* TPM command header size */
#define TPM_HEADER_SIZE        10
#define TPM_RETRY      5
/*
 * I2C bus device maximum buffer size w/o counting I2C address or command
 * i.e. max size required for I2C write is 34 = addr, command, 32 bytes data
 */
#define TPM_I2C_MAX_BUF_SIZE           32
#define TPM_I2C_RETRY_COUNT            32
#define TPM_I2C_BUS_DELAY              1       /* msec */
#define TPM_I2C_RETRY_DELAY_SHORT      2       /* msec */
#define TPM_I2C_RETRY_DELAY_LONG       10      /* msec */

#define I2C_DRIVER_NAME "tpm_i2c_nuvoton"

struct priv_data {
	int irq;
	unsigned int intrs;
	wait_queue_head_t read_queue;
};

static s32 i2c_nuvoton_read_buf(struct i2c_client *client, u8 offset, u8 size,
				u8 *data)
{
	s32 status;

	status = i2c_smbus_read_i2c_block_data(client, offset, size, data);
	dev_dbg(&client->dev,
		"%s(offset=%u size=%u data=%*ph) -> sts=%d\n", __func__,
		offset, size, (int)size, data, status);
	return status;
}

static s32 i2c_nuvoton_write_buf(struct i2c_client *client, u8 offset, u8 size,
				 u8 *data)
{
	s32 status;

	status = i2c_smbus_write_i2c_block_data(client, offset, size, data);
	dev_dbg(&client->dev,
		"%s(offset=%u size=%u data=%*ph) -> sts=%d\n", __func__,
		offset, size, (int)size, data, status);
	return status;
}

#define TPM_STS_VALID          0x80
#define TPM_STS_COMMAND_READY  0x40
#define TPM_STS_GO             0x20
#define TPM_STS_DATA_AVAIL     0x10
#define TPM_STS_EXPECT         0x08
#define TPM_STS_RESPONSE_RETRY 0x02
#define TPM_STS_ERR_VAL        0x07    /* bit2...bit0 reads always 0 */

#define TPM_I2C_SHORT_TIMEOUT  750     /* ms */
#define TPM_I2C_LONG_TIMEOUT   2000    /* 2 sec */

/* read TPM_STS register */
static u8 i2c_nuvoton_read_status(struct tpm_chip *chip)
{
	struct i2c_client *client = to_i2c_client(chip->dev.parent);
	s32 status;
	u8 data;

	status = i2c_nuvoton_read_buf(client, TPM_STS, 1, &data);
	if (status <= 0) {
		dev_err(&chip->dev, "%s() error return %d\n", __func__,
			status);
		data = TPM_STS_ERR_VAL;
	}

	return data;
}

/* write byte to TPM_STS register */
static s32 i2c_nuvoton_write_status(struct i2c_client *client, u8 data)
{
	s32 status;
	int i;

	/* this causes the current command to be aborted */
	for (i = 0, status = -1; i < TPM_I2C_RETRY_COUNT && status < 0; i++) {
		status = i2c_nuvoton_write_buf(client, TPM_STS, 1, &data);
		msleep(TPM_I2C_BUS_DELAY);
	}
	return status;
}

/* write commandReady to TPM_STS register */
static void i2c_nuvoton_ready(struct tpm_chip *chip)
{
	struct i2c_client *client = to_i2c_client(chip->dev.parent);
	s32 status;

	/* this causes the current command to be aborted */
	status = i2c_nuvoton_write_status(client, TPM_STS_COMMAND_READY);
	if (status < 0)
		dev_err(&chip->dev,
			"%s() fail to write TPM_STS.commandReady\n", __func__);
}

/* read burstCount field from TPM_STS register
 * return -1 on fail to read */
static int i2c_nuvoton_get_burstcount(struct i2c_client *client,
				      struct tpm_chip *chip)
{
	unsigned long stop = jiffies + chip->timeout_d;
	s32 status;
	int burst_count = -1;
	u8 data;

	/* wait for burstcount to be non-zero */
	do {
		/* in I2C burstCount is 1 byte */
		status = i2c_nuvoton_read_buf(client, TPM_BURST_COUNT, 1,
					      &data);
		if (status > 0 && data > 0) {
			burst_count = min_t(u8, TPM_I2C_MAX_BUF_SIZE, data);
			break;
		}
		msleep(TPM_I2C_BUS_DELAY);
	} while (time_before(jiffies, stop));

	return burst_count;
}

/*
 * WPCT301/NPCT501 SINT# supports only dataAvail
 * any call to this function which is not waiting for dataAvail will
 * set queue to NULL to avoid waiting for interrupt
 */
static bool i2c_nuvoton_check_status(struct tpm_chip *chip, u8 mask, u8 value)
{
	u8 status = i2c_nuvoton_read_status(chip);
	return (status != TPM_STS_ERR_VAL) && ((status & mask) == value);
}

static int i2c_nuvoton_wait_for_stat(struct tpm_chip *chip, u8 mask, u8 value,
				     u32 timeout, wait_queue_head_t *queue)
{
	if ((chip->flags & TPM_CHIP_FLAG_IRQ) && queue) {
		s32 rc;
		struct priv_data *priv = dev_get_drvdata(&chip->dev);
		unsigned int cur_intrs = priv->intrs;

		enable_irq(priv->irq);
		rc = wait_event_interruptible_timeout(*queue,
						      cur_intrs != priv->intrs,
						      timeout);
		if (rc > 0)
			return 0;
		/* At this point we know that the SINT pin is asserted, so we
		 * do not need to do i2c_nuvoton_check_status */
	} else {
		unsigned long ten_msec, stop;
		bool status_valid;

		/* check current status */
		status_valid = i2c_nuvoton_check_status(chip, mask, value);
		if (status_valid)
			return 0;

		/* use polling to wait for the event */
		ten_msec = jiffies + msecs_to_jiffies(TPM_I2C_RETRY_DELAY_LONG);
		stop = jiffies + timeout;
		do {
			if (time_before(jiffies, ten_msec))
				msleep(TPM_I2C_RETRY_DELAY_SHORT);
			else
				msleep(TPM_I2C_RETRY_DELAY_LONG);
			status_valid = i2c_nuvoton_check_status(chip, mask,
								value);
			if (status_valid)
				return 0;
		} while (time_before(jiffies, stop));
	}
	dev_err(&chip->dev, "%s(%02x, %02x) -> timeout\n", __func__, mask,
		value);
	return -ETIMEDOUT;
}

/* wait for dataAvail field to be set in the TPM_STS register */
static int i2c_nuvoton_wait_for_data_avail(struct tpm_chip *chip, u32 timeout,
					   wait_queue_head_t *queue)
{
	return i2c_nuvoton_wait_for_stat(chip,
					 TPM_STS_DATA_AVAIL | TPM_STS_VALID,
					 TPM_STS_DATA_AVAIL | TPM_STS_VALID,
					 timeout, queue);
}

/* Read @count bytes into @buf from TPM_RD_FIFO register */
static int i2c_nuvoton_recv_data(struct i2c_client *client,
				 struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct priv_data *priv = dev_get_drvdata(&chip->dev);
	s32 rc;
	int burst_count, bytes2read, size = 0;

	while (size < count &&
	       i2c_nuvoton_wait_for_data_avail(chip,
					       chip->timeout_c,
					       &priv->read_queue) == 0) {
		burst_count = i2c_nuvoton_get_burstcount(client, chip);
		if (burst_count < 0) {
			dev_err(&chip->dev,
				"%s() fail to read burstCount=%d\n", __func__,
				burst_count);
			return -EIO;
		}
		bytes2read = min_t(size_t, burst_count, count - size);
		rc = i2c_nuvoton_read_buf(client, TPM_DATA_FIFO_R,
					  bytes2read, &buf[size]);
		if (rc < 0) {
			dev_err(&chip->dev,
				"%s() fail on i2c_nuvoton_read_buf()=%d\n",
				__func__, rc);
			return -EIO;
		}
		dev_dbg(&chip->dev, "%s(%d):", __func__, bytes2read);
		size += bytes2read;
	}

	return size;
}

/* Read TPM command results */
static int i2c_nuvoton_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct priv_data *priv = dev_get_drvdata(&chip->dev);
	struct device *dev = chip->dev.parent;
	struct i2c_client *client = to_i2c_client(dev);
	s32 rc;
	int expected, status, burst_count, retries, size = 0;

	if (count < TPM_HEADER_SIZE) {
		i2c_nuvoton_ready(chip);    /* return to idle */
		dev_err(dev, "%s() count < header size\n", __func__);
		return -EIO;
	}
	for (retries = 0; retries < TPM_RETRY; retries++) {
		if (retries > 0) {
			/* if this is not the first trial, set responseRetry */
			i2c_nuvoton_write_status(client,
						 TPM_STS_RESPONSE_RETRY);
		}
		/*
		 * read first available (> 10 bytes), including:
		 * tag, paramsize, and result
		 */
		status = i2c_nuvoton_wait_for_data_avail(
			chip, chip->timeout_c, &priv->read_queue);
		if (status != 0) {
			dev_err(dev, "%s() timeout on dataAvail\n", __func__);
			size = -ETIMEDOUT;
			continue;
		}
		burst_count = i2c_nuvoton_get_burstcount(client, chip);
		if (burst_count < 0) {
			dev_err(dev, "%s() fail to get burstCount\n", __func__);
			size = -EIO;
			continue;
		}
		size = i2c_nuvoton_recv_data(client, chip, buf,
					     burst_count);
		if (size < TPM_HEADER_SIZE) {
			dev_err(dev, "%s() fail to read header\n", __func__);
			size = -EIO;
			continue;
		}
		/*
		 * convert number of expected bytes field from big endian 32 bit
		 * to machine native
		 */
		expected = be32_to_cpu(*(__be32 *) (buf + 2));
		if (expected > count) {
			dev_err(dev, "%s() expected > count\n", __func__);
			size = -EIO;
			continue;
		}
		rc = i2c_nuvoton_recv_data(client, chip, &buf[size],
					   expected - size);
		size += rc;
		if (rc < 0 || size < expected) {
			dev_err(dev, "%s() fail to read remainder of result\n",
				__func__);
			size = -EIO;
			continue;
		}
		if (i2c_nuvoton_wait_for_stat(
			    chip, TPM_STS_VALID | TPM_STS_DATA_AVAIL,
			    TPM_STS_VALID, chip->timeout_c,
			    NULL)) {
			dev_err(dev, "%s() error left over data\n", __func__);
			size = -ETIMEDOUT;
			continue;
		}
		break;
	}
	i2c_nuvoton_ready(chip);
	dev_dbg(&chip->dev, "%s() -> %d\n", __func__, size);
	return size;
}

/*
 * Send TPM command.
 *
 * If interrupts are used (signaled by an irq set in the vendor structure)
 * tpm.c can skip polling for the data to be available as the interrupt is
 * waited for here
 */
static int i2c_nuvoton_send(struct tpm_chip *chip, u8 *buf, size_t len)
{
	struct priv_data *priv = dev_get_drvdata(&chip->dev);
	struct device *dev = chip->dev.parent;
	struct i2c_client *client = to_i2c_client(dev);
	u32 ordinal;
	size_t count = 0;
	int burst_count, bytes2write, retries, rc = -EIO;

	for (retries = 0; retries < TPM_RETRY; retries++) {
		i2c_nuvoton_ready(chip);
		if (i2c_nuvoton_wait_for_stat(chip, TPM_STS_COMMAND_READY,
					      TPM_STS_COMMAND_READY,
					      chip->timeout_b, NULL)) {
			dev_err(dev, "%s() timeout on commandReady\n",
				__func__);
			rc = -EIO;
			continue;
		}
		rc = 0;
		while (count < len - 1) {
			burst_count = i2c_nuvoton_get_burstcount(client,
								 chip);
			if (burst_count < 0) {
				dev_err(dev, "%s() fail get burstCount\n",
					__func__);
				rc = -EIO;
				break;
			}
			bytes2write = min_t(size_t, burst_count,
					    len - 1 - count);
			rc = i2c_nuvoton_write_buf(client, TPM_DATA_FIFO_W,
						   bytes2write, &buf[count]);
			if (rc < 0) {
				dev_err(dev, "%s() fail i2cWriteBuf\n",
					__func__);
				break;
			}
			dev_dbg(dev, "%s(%d):", __func__, bytes2write);
			count += bytes2write;
			rc = i2c_nuvoton_wait_for_stat(chip,
						       TPM_STS_VALID |
						       TPM_STS_EXPECT,
						       TPM_STS_VALID |
						       TPM_STS_EXPECT,
						       chip->timeout_c,
						       NULL);
			if (rc < 0) {
				dev_err(dev, "%s() timeout on Expect\n",
					__func__);
				rc = -ETIMEDOUT;
				break;
			}
		}
		if (rc < 0)
			continue;

		/* write last byte */
		rc = i2c_nuvoton_write_buf(client, TPM_DATA_FIFO_W, 1,
					   &buf[count]);
		if (rc < 0) {
			dev_err(dev, "%s() fail to write last byte\n",
				__func__);
			rc = -EIO;
			continue;
		}
		dev_dbg(dev, "%s(last): %02x", __func__, buf[count]);
		rc = i2c_nuvoton_wait_for_stat(chip,
					       TPM_STS_VALID | TPM_STS_EXPECT,
					       TPM_STS_VALID,
					       chip->timeout_c, NULL);
		if (rc) {
			dev_err(dev, "%s() timeout on Expect to clear\n",
				__func__);
			rc = -ETIMEDOUT;
			continue;
		}
		break;
	}
	if (rc < 0) {
		/* retries == TPM_RETRY */
		i2c_nuvoton_ready(chip);
		return rc;
	}
	/* execute the TPM command */
	rc = i2c_nuvoton_write_status(client, TPM_STS_GO);
	if (rc < 0) {
		dev_err(dev, "%s() fail to write Go\n", __func__);
		i2c_nuvoton_ready(chip);
		return rc;
	}
	ordinal = be32_to_cpu(*((__be32 *) (buf + 6)));
	rc = i2c_nuvoton_wait_for_data_avail(chip,
					     tpm_calc_ordinal_duration(chip,
								       ordinal),
					     &priv->read_queue);
	if (rc) {
		dev_err(dev, "%s() timeout command duration\n", __func__);
		i2c_nuvoton_ready(chip);
		return rc;
	}

	dev_dbg(dev, "%s() -> %zd\n", __func__, len);
	return len;
}

static bool i2c_nuvoton_req_canceled(struct tpm_chip *chip, u8 status)
{
	return (status == TPM_STS_COMMAND_READY);
}

static const struct tpm_class_ops tpm_i2c = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.status = i2c_nuvoton_read_status,
	.recv = i2c_nuvoton_recv,
	.send = i2c_nuvoton_send,
	.cancel = i2c_nuvoton_ready,
	.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
	.req_canceled = i2c_nuvoton_req_canceled,
};

/* The only purpose for the handler is to signal to any waiting threads that
 * the interrupt is currently being asserted. The driver does not do any
 * processing triggered by interrupts, and the chip provides no way to mask at
 * the source (plus that would be slow over I2C). Run the IRQ as a one-shot,
 * this means it cannot be shared. */
static irqreturn_t i2c_nuvoton_int_handler(int dummy, void *dev_id)
{
	struct tpm_chip *chip = dev_id;
	struct priv_data *priv = dev_get_drvdata(&chip->dev);

	priv->intrs++;
	wake_up(&priv->read_queue);
	disable_irq_nosync(priv->irq);
	return IRQ_HANDLED;
}

static int get_vid(struct i2c_client *client, u32 *res)
{
	static const u8 vid_did_rid_value[] = { 0x50, 0x10, 0xfe };
	u32 temp;
	s32 rc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;
	rc = i2c_nuvoton_read_buf(client, TPM_VID_DID_RID, 4, (u8 *)&temp);
	if (rc < 0)
		return rc;

	/* check WPCT301 values - ignore RID */
	if (memcmp(&temp, vid_did_rid_value, sizeof(vid_did_rid_value))) {
		/*
		 * f/w rev 2.81 has an issue where the VID_DID_RID is not
		 * reporting the right value. so give it another chance at
		 * offset 0x20 (FIFO_W).
		 */
		rc = i2c_nuvoton_read_buf(client, TPM_DATA_FIFO_W, 4,
					  (u8 *) (&temp));
		if (rc < 0)
			return rc;

		/* check WPCT301 values - ignore RID */
		if (memcmp(&temp, vid_did_rid_value,
			   sizeof(vid_did_rid_value)))
			return -ENODEV;
	}

	*res = temp;
	return 0;
}

static int i2c_nuvoton_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int rc;
	struct tpm_chip *chip;
	struct device *dev = &client->dev;
	struct priv_data *priv;
	u32 vid = 0;

	rc = get_vid(client, &vid);
	if (rc)
		return rc;

	dev_info(dev, "VID: %04X DID: %02X RID: %02X\n", (u16) vid,
		 (u8) (vid >> 16), (u8) (vid >> 24));

	chip = tpmm_chip_alloc(dev, &tpm_i2c);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	priv = devm_kzalloc(dev, sizeof(struct priv_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_waitqueue_head(&priv->read_queue);

	/* Default timeouts */
	chip->timeout_a = msecs_to_jiffies(TPM_I2C_SHORT_TIMEOUT);
	chip->timeout_b = msecs_to_jiffies(TPM_I2C_LONG_TIMEOUT);
	chip->timeout_c = msecs_to_jiffies(TPM_I2C_SHORT_TIMEOUT);
	chip->timeout_d = msecs_to_jiffies(TPM_I2C_SHORT_TIMEOUT);

	dev_set_drvdata(&chip->dev, priv);

	/*
	 * I2C intfcaps (interrupt capabilitieis) in the chip are hard coded to:
	 *   TPM_INTF_INT_LEVEL_LOW | TPM_INTF_DATA_AVAIL_INT
	 * The IRQ should be set in the i2c_board_info (which is done
	 * automatically in of_i2c_register_devices, for device tree users */
	priv->irq = client->irq;
	if (client->irq) {
		dev_dbg(dev, "%s() priv->irq\n", __func__);
		rc = devm_request_irq(dev, client->irq,
				      i2c_nuvoton_int_handler,
				      IRQF_TRIGGER_LOW,
				      dev_name(&chip->dev),
				      chip);
		if (rc) {
			dev_err(dev, "%s() Unable to request irq: %d for use\n",
				__func__, priv->irq);
			priv->irq = 0;
		} else {
			chip->flags |= TPM_CHIP_FLAG_IRQ;
			/* Clear any pending interrupt */
			i2c_nuvoton_ready(chip);
			/* - wait for TPM_STS==0xA0 (stsValid, commandReady) */
			rc = i2c_nuvoton_wait_for_stat(chip,
						       TPM_STS_COMMAND_READY,
						       TPM_STS_COMMAND_READY,
						       chip->timeout_b,
						       NULL);
			if (rc == 0) {
				/*
				 * TIS is in ready state
				 * write dummy byte to enter reception state
				 * TPM_DATA_FIFO_W <- rc (0)
				 */
				rc = i2c_nuvoton_write_buf(client,
							   TPM_DATA_FIFO_W,
							   1, (u8 *) (&rc));
				if (rc < 0)
					return rc;
				/* TPM_STS <- 0x40 (commandReady) */
				i2c_nuvoton_ready(chip);
			} else {
				/*
				 * timeout_b reached - command was
				 * aborted. TIS should now be in idle state -
				 * only TPM_STS_VALID should be set
				 */
				if (i2c_nuvoton_read_status(chip) !=
				    TPM_STS_VALID)
					return -EIO;
			}
		}
	}

	return tpm_chip_register(chip);
}

static int i2c_nuvoton_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = i2c_get_clientdata(client);

	tpm_chip_unregister(chip);
	return 0;
}

static const struct i2c_device_id i2c_nuvoton_id[] = {
	{I2C_DRIVER_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, i2c_nuvoton_id);

#ifdef CONFIG_OF
static const struct of_device_id i2c_nuvoton_of_match[] = {
	{.compatible = "nuvoton,npct501"},
	{.compatible = "winbond,wpct301"},
	{},
};
MODULE_DEVICE_TABLE(of, i2c_nuvoton_of_match);
#endif

static SIMPLE_DEV_PM_OPS(i2c_nuvoton_pm_ops, tpm_pm_suspend, tpm_pm_resume);

static struct i2c_driver i2c_nuvoton_driver = {
	.id_table = i2c_nuvoton_id,
	.probe = i2c_nuvoton_probe,
	.remove = i2c_nuvoton_remove,
	.driver = {
		.name = I2C_DRIVER_NAME,
		.pm = &i2c_nuvoton_pm_ops,
		.of_match_table = of_match_ptr(i2c_nuvoton_of_match),
	},
};

module_i2c_driver(i2c_nuvoton_driver);

MODULE_AUTHOR("Dan Morav (dan.morav@nuvoton.com)");
MODULE_DESCRIPTION("Nuvoton TPM I2C Driver");
MODULE_LICENSE("GPL");
