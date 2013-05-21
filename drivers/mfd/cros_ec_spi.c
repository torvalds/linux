/*
 * ChromeOS EC multi-function device (SPI)
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>


/* The header byte, which follows the preamble */
#define EC_MSG_HEADER			0xec

/*
 * Number of EC preamble bytes we read at a time. Since it takes
 * about 400-500us for the EC to respond there is not a lot of
 * point in tuning this. If the EC could respond faster then
 * we could increase this so that might expect the preamble and
 * message to occur in a single transaction. However, the maximum
 * SPI transfer size is 256 bytes, so at 5MHz we need a response
 * time of perhaps <320us (200 bytes / 1600 bits).
 */
#define EC_MSG_PREAMBLE_COUNT		32

/*
  * We must get a response from the EC in 5ms. This is a very long
  * time, but the flash write command can take 2-3ms. The EC command
  * processing is currently not very fast (about 500us). We could
  * look at speeding this up and making the flash write command a
  * 'slow' command, requiring a GET_STATUS wait loop, like flash
  * erase.
  */
#define EC_MSG_DEADLINE_MS		5

/*
  * Time between raising the SPI chip select (for the end of a
  * transaction) and dropping it again (for the next transaction).
  * If we go too fast, the EC will miss the transaction. It seems
  * that 50us is enough with the 16MHz STM32 EC.
  */
#define EC_SPI_RECOVERY_TIME_NS	(50 * 1000)

/**
 * struct cros_ec_spi - information about a SPI-connected EC
 *
 * @spi: SPI device we are connected to
 * @last_transfer_ns: time that we last finished a transfer, or 0 if there
 *	if no record
 */
struct cros_ec_spi {
	struct spi_device *spi;
	s64 last_transfer_ns;
};

static void debug_packet(struct device *dev, const char *name, u8 *ptr,
			  int len)
{
#ifdef DEBUG
	int i;

	dev_dbg(dev, "%s: ", name);
	for (i = 0; i < len; i++)
		dev_cont(dev, " %02x", ptr[i]);
#endif
}

/**
 * cros_ec_spi_receive_response - Receive a response from the EC.
 *
 * This function has two phases: reading the preamble bytes (since if we read
 * data from the EC before it is ready to send, we just get preamble) and
 * reading the actual message.
 *
 * The received data is placed into ec_dev->din.
 *
 * @ec_dev: ChromeOS EC device
 * @need_len: Number of message bytes we need to read
 */
static int cros_ec_spi_receive_response(struct cros_ec_device *ec_dev,
					int need_len)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_transfer trans;
	struct spi_message msg;
	u8 *ptr, *end;
	int ret;
	unsigned long deadline;
	int todo;

	/* Receive data until we see the header byte */
	deadline = jiffies + msecs_to_jiffies(EC_MSG_DEADLINE_MS);
	do {
		memset(&trans, '\0', sizeof(trans));
		trans.cs_change = 1;
		trans.rx_buf = ptr = ec_dev->din;
		trans.len = EC_MSG_PREAMBLE_COUNT;

		spi_message_init(&msg);
		spi_message_add_tail(&trans, &msg);
		ret = spi_sync(ec_spi->spi, &msg);
		if (ret < 0) {
			dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);
			return ret;
		}

		for (end = ptr + EC_MSG_PREAMBLE_COUNT; ptr != end; ptr++) {
			if (*ptr == EC_MSG_HEADER) {
				dev_dbg(ec_dev->dev, "msg found at %ld\n",
					ptr - ec_dev->din);
				break;
			}
		}

		if (time_after(jiffies, deadline)) {
			dev_warn(ec_dev->dev, "EC failed to respond in time\n");
			return -ETIMEDOUT;
		}
	} while (ptr == end);

	/*
	 * ptr now points to the header byte. Copy any valid data to the
	 * start of our buffer
	 */
	todo = end - ++ptr;
	BUG_ON(todo < 0 || todo > ec_dev->din_size);
	todo = min(todo, need_len);
	memmove(ec_dev->din, ptr, todo);
	ptr = ec_dev->din + todo;
	dev_dbg(ec_dev->dev, "need %d, got %d bytes from preamble\n",
		 need_len, todo);
	need_len -= todo;

	/* Receive data until we have it all */
	while (need_len > 0) {
		/*
		 * We can't support transfers larger than the SPI FIFO size
		 * unless we have DMA. We don't have DMA on the ISP SPI ports
		 * for Exynos. We need a way of asking SPI driver for
		 * maximum-supported transfer size.
		 */
		todo = min(need_len, 256);
		dev_dbg(ec_dev->dev, "loop, todo=%d, need_len=%d, ptr=%ld\n",
			todo, need_len, ptr - ec_dev->din);

		memset(&trans, '\0', sizeof(trans));
		trans.cs_change = 1;
		trans.rx_buf = ptr;
		trans.len = todo;
		spi_message_init(&msg);
		spi_message_add_tail(&trans, &msg);

		/* send command to EC and read answer */
		BUG_ON((u8 *)trans.rx_buf - ec_dev->din + todo >
				ec_dev->din_size);
		ret = spi_sync(ec_spi->spi, &msg);
		if (ret < 0) {
			dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);
			return ret;
		}

		debug_packet(ec_dev->dev, "interim", ptr, todo);
		ptr += todo;
		need_len -= todo;
	}

	dev_dbg(ec_dev->dev, "loop done, ptr=%ld\n", ptr - ec_dev->din);

	return 0;
}

/**
 * cros_ec_command_spi_xfer - Transfer a message over SPI and receive the reply
 *
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 */
static int cros_ec_command_spi_xfer(struct cros_ec_device *ec_dev,
				    struct cros_ec_msg *ec_msg)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_transfer trans;
	struct spi_message msg;
	int i, len;
	u8 *ptr;
	int sum;
	int ret = 0, final_ret;
	struct timespec ts;

	len = cros_ec_prepare_tx(ec_dev, ec_msg);
	dev_dbg(ec_dev->dev, "prepared, len=%d\n", len);

	/* If it's too soon to do another transaction, wait */
	if (ec_spi->last_transfer_ns) {
		struct timespec ts;
		unsigned long delay;	/* The delay completed so far */

		ktime_get_ts(&ts);
		delay = timespec_to_ns(&ts) - ec_spi->last_transfer_ns;
		if (delay < EC_SPI_RECOVERY_TIME_NS)
			ndelay(delay);
	}

	/* Transmit phase - send our message */
	debug_packet(ec_dev->dev, "out", ec_dev->dout, len);
	memset(&trans, '\0', sizeof(trans));
	trans.tx_buf = ec_dev->dout;
	trans.len = len;
	trans.cs_change = 1;
	spi_message_init(&msg);
	spi_message_add_tail(&trans, &msg);
	ret = spi_sync(ec_spi->spi, &msg);

	/* Get the response */
	if (!ret) {
		ret = cros_ec_spi_receive_response(ec_dev,
				ec_msg->in_len + EC_MSG_TX_PROTO_BYTES);
	} else {
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);
	}

	/* turn off CS */
	spi_message_init(&msg);
	final_ret = spi_sync(ec_spi->spi, &msg);
	ktime_get_ts(&ts);
	ec_spi->last_transfer_ns = timespec_to_ns(&ts);
	if (!ret)
		ret = final_ret;
	if (ret < 0) {
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);
		return ret;
	}

	/* check response error code */
	ptr = ec_dev->din;
	if (ptr[0]) {
		dev_warn(ec_dev->dev, "command 0x%02x returned an error %d\n",
			 ec_msg->cmd, ptr[0]);
		debug_packet(ec_dev->dev, "in_err", ptr, len);
		return -EINVAL;
	}
	len = ptr[1];
	sum = ptr[0] + ptr[1];
	if (len > ec_msg->in_len) {
		dev_err(ec_dev->dev, "packet too long (%d bytes, expected %d)",
			len, ec_msg->in_len);
		return -ENOSPC;
	}

	/* copy response packet payload and compute checksum */
	for (i = 0; i < len; i++) {
		sum += ptr[i + 2];
		if (ec_msg->in_len)
			ec_msg->in_buf[i] = ptr[i + 2];
	}
	sum &= 0xff;

	debug_packet(ec_dev->dev, "in", ptr, len + 3);

	if (sum != ptr[len + 2]) {
		dev_err(ec_dev->dev,
			"bad packet checksum, expected %02x, got %02x\n",
			sum, ptr[len + 2]);
		return -EBADMSG;
	}

	return 0;
}

static int cros_ec_probe_spi(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct cros_ec_device *ec_dev;
	struct cros_ec_spi *ec_spi;
	int err;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	err = spi_setup(spi);
	if (err < 0)
		return err;

	ec_spi = devm_kzalloc(dev, sizeof(*ec_spi), GFP_KERNEL);
	if (ec_spi == NULL)
		return -ENOMEM;
	ec_spi->spi = spi;
	ec_dev = devm_kzalloc(dev, sizeof(*ec_dev), GFP_KERNEL);
	if (!ec_dev)
		return -ENOMEM;

	spi_set_drvdata(spi, ec_dev);
	ec_dev->name = "SPI";
	ec_dev->dev = dev;
	ec_dev->priv = ec_spi;
	ec_dev->irq = spi->irq;
	ec_dev->command_xfer = cros_ec_command_spi_xfer;
	ec_dev->ec_name = ec_spi->spi->modalias;
	ec_dev->phys_name = dev_name(&ec_spi->spi->dev);
	ec_dev->parent = &ec_spi->spi->dev;
	ec_dev->din_size = EC_MSG_BYTES + EC_MSG_PREAMBLE_COUNT;
	ec_dev->dout_size = EC_MSG_BYTES;

	err = cros_ec_register(ec_dev);
	if (err) {
		dev_err(dev, "cannot register EC\n");
		return err;
	}

	return 0;
}

static int cros_ec_remove_spi(struct spi_device *spi)
{
	struct cros_ec_device *ec_dev;

	ec_dev = spi_get_drvdata(spi);
	cros_ec_remove(ec_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cros_ec_spi_suspend(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_suspend(ec_dev);
}

static int cros_ec_spi_resume(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_resume(ec_dev);
}
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_spi_pm_ops, cros_ec_spi_suspend,
			 cros_ec_spi_resume);

static const struct spi_device_id cros_ec_spi_id[] = {
	{ "cros-ec-spi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, cros_ec_spi_id);

static struct spi_driver cros_ec_driver_spi = {
	.driver	= {
		.name	= "cros-ec-spi",
		.owner	= THIS_MODULE,
		.pm	= &cros_ec_spi_pm_ops,
	},
	.probe		= cros_ec_probe_spi,
	.remove		= cros_ec_remove_spi,
	.id_table	= cros_ec_spi_id,
};

module_spi_driver(cros_ec_driver_spi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC multi function device (SPI)");
