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
#include <linux/of.h>
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
 * Allow for a long time for the EC to respond.  We support i2c
 * tunneling and support fairly long messages for the tunnel (249
 * bytes long at the moment).  If we're talking to a 100 kHz device
 * on the other end and need to transfer ~256 bytes, then we need:
 *  10 us/bit * ~10 bits/byte * ~256 bytes = ~25ms
 *
 * We'll wait 4 times that to handle clock stretching and other
 * paranoia.
 *
 * It's pretty unlikely that we'll really see a 249 byte tunnel in
 * anything other than testing.  If this was more common we might
 * consider having slow commands like this require a GET_STATUS
 * wait loop.  The 'flash write' command would be another candidate
 * for this, clocking in at 2-3ms.
 */
#define EC_MSG_DEADLINE_MS		100

/*
  * Time between raising the SPI chip select (for the end of a
  * transaction) and dropping it again (for the next transaction).
  * If we go too fast, the EC will miss the transaction. We know that we
  * need at least 70 us with the 16 MHz STM32 EC, so go with 200 us to be
  * safe.
  */
#define EC_SPI_RECOVERY_TIME_NS	(200 * 1000)

/*
 * The EC is unresponsive for a time after a reboot command.  Add a
 * simple delay to make sure that the bus stays locked.
 */
#define EC_REBOOT_DELAY_MS	50

/**
 * struct cros_ec_spi - information about a SPI-connected EC
 *
 * @spi: SPI device we are connected to
 * @last_transfer_ns: time that we last finished a transfer, or 0 if there
 *	if no record
 * @end_of_msg_delay: used to set the delay_usecs on the spi_transfer that
 *      is sent when we want to turn off CS at the end of a transaction.
 */
struct cros_ec_spi {
	struct spi_device *spi;
	s64 last_transfer_ns;
	unsigned int end_of_msg_delay;
};

static void debug_packet(struct device *dev, const char *name, u8 *ptr,
			  int len)
{
#ifdef DEBUG
	int i;

	dev_dbg(dev, "%s: ", name);
	for (i = 0; i < len; i++)
		pr_cont(" %02x", ptr[i]);

	pr_cont("\n");
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
	while (true) {
		unsigned long start_jiffies = jiffies;

		memset(&trans, 0, sizeof(trans));
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
				dev_dbg(ec_dev->dev, "msg found at %zd\n",
					ptr - ec_dev->din);
				break;
			}
		}
		if (ptr != end)
			break;

		/*
		 * Use the time at the start of the loop as a timeout.  This
		 * gives us one last shot at getting the transfer and is useful
		 * in case we got context switched out for a while.
		 */
		if (time_after(start_jiffies, deadline)) {
			dev_warn(ec_dev->dev, "EC failed to respond in time\n");
			return -ETIMEDOUT;
		}
	}

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
		dev_dbg(ec_dev->dev, "loop, todo=%d, need_len=%d, ptr=%zd\n",
			todo, need_len, ptr - ec_dev->din);

		memset(&trans, 0, sizeof(trans));
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

	dev_dbg(ec_dev->dev, "loop done, ptr=%zd\n", ptr - ec_dev->din);

	return 0;
}

/**
 * cros_ec_cmd_xfer_spi - Transfer a message over SPI and receive the reply
 *
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 */
static int cros_ec_cmd_xfer_spi(struct cros_ec_device *ec_dev,
				struct cros_ec_command *ec_msg)
{
	struct cros_ec_spi *ec_spi = ec_dev->priv;
	struct spi_transfer trans;
	struct spi_message msg;
	int i, len;
	u8 *ptr;
	int sum;
	int ret = 0, final_ret;

	len = cros_ec_prepare_tx(ec_dev, ec_msg);
	dev_dbg(ec_dev->dev, "prepared, len=%d\n", len);

	/* If it's too soon to do another transaction, wait */
	if (ec_spi->last_transfer_ns) {
		unsigned long delay;	/* The delay completed so far */

		delay = ktime_get_ns() - ec_spi->last_transfer_ns;
		if (delay < EC_SPI_RECOVERY_TIME_NS)
			ndelay(EC_SPI_RECOVERY_TIME_NS - delay);
	}

	/* Transmit phase - send our message */
	debug_packet(ec_dev->dev, "out", ec_dev->dout, len);
	memset(&trans, 0, sizeof(trans));
	trans.tx_buf = ec_dev->dout;
	trans.len = len;
	trans.cs_change = 1;
	spi_message_init(&msg);
	spi_message_add_tail(&trans, &msg);
	ret = spi_sync(ec_spi->spi, &msg);

	/* Get the response */
	if (!ret) {
		ret = cros_ec_spi_receive_response(ec_dev,
				ec_msg->insize + EC_MSG_TX_PROTO_BYTES);
	} else {
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);
	}

	/*
	 * Turn off CS, possibly adding a delay to ensure the rising edge
	 * doesn't come too soon after the end of the data.
	 */
	spi_message_init(&msg);
	memset(&trans, 0, sizeof(trans));
	trans.delay_usecs = ec_spi->end_of_msg_delay;
	spi_message_add_tail(&trans, &msg);

	final_ret = spi_sync(ec_spi->spi, &msg);
	ec_spi->last_transfer_ns = ktime_get_ns();
	if (!ret)
		ret = final_ret;
	if (ret < 0) {
		dev_err(ec_dev->dev, "spi transfer failed: %d\n", ret);
		goto exit;
	}

	ptr = ec_dev->din;

	/* check response error code */
	ec_msg->result = ptr[0];
	ret = cros_ec_check_result(ec_dev, ec_msg);
	if (ret)
		goto exit;

	len = ptr[1];
	sum = ptr[0] + ptr[1];
	if (len > ec_msg->insize) {
		dev_err(ec_dev->dev, "packet too long (%d bytes, expected %d)",
			len, ec_msg->insize);
		ret = -ENOSPC;
		goto exit;
	}

	/* copy response packet payload and compute checksum */
	for (i = 0; i < len; i++) {
		sum += ptr[i + 2];
		if (ec_msg->insize)
			ec_msg->indata[i] = ptr[i + 2];
	}
	sum &= 0xff;

	debug_packet(ec_dev->dev, "in", ptr, len + 3);

	if (sum != ptr[len + 2]) {
		dev_err(ec_dev->dev,
			"bad packet checksum, expected %02x, got %02x\n",
			sum, ptr[len + 2]);
		ret = -EBADMSG;
		goto exit;
	}

	ret = len;
exit:
	if (ec_msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

static void cros_ec_spi_dt_probe(struct cros_ec_spi *ec_spi, struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val;
	int ret;

	ret = of_property_read_u32(np, "google,cros-ec-spi-msg-delay", &val);
	if (!ret)
		ec_spi->end_of_msg_delay = val;
}

static int cros_ec_spi_probe(struct spi_device *spi)
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

	/* Check for any DT properties */
	cros_ec_spi_dt_probe(ec_spi, dev);

	spi_set_drvdata(spi, ec_dev);
	ec_dev->dev = dev;
	ec_dev->priv = ec_spi;
	ec_dev->irq = spi->irq;
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_spi;
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

	device_init_wakeup(&spi->dev, true);

	return 0;
}

static int cros_ec_spi_remove(struct spi_device *spi)
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
	.probe		= cros_ec_spi_probe,
	.remove		= cros_ec_spi_remove,
	.id_table	= cros_ec_spi_id,
};

module_spi_driver(cros_ec_driver_spi);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC multi function device (SPI)");
