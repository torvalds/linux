/*
 * SPI slave handler reporting uptime at reception of previous SPI message
 *
 * This SPI slave handler sends the time of reception of the last SPI message
 * as two 32-bit unsigned integers in binary format and in network byte order,
 * representing the number of seconds and fractional seconds (in microseconds)
 * since boot up.
 *
 * Copyright (C) 2016-2017 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Usage (assuming /dev/spidev2.0 corresponds to the SPI master on the remote
 * system):
 *
 *   # spidev_test -D /dev/spidev2.0 -p dummy-8B
 *   spi mode: 0x0
 *   bits per word: 8
 *   max speed: 500000 Hz (500 KHz)
 *   RX | 00 00 04 6D 00 09 5B BB ...
 *		^^^^^    ^^^^^^^^
 *		seconds  microseconds
 */

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#include <linux/spi/spi.h>


struct spi_slave_time_priv {
	struct spi_device *spi;
	struct completion finished;
	struct spi_transfer xfer;
	struct spi_message msg;
	__be32 buf[2];
};

static int spi_slave_time_submit(struct spi_slave_time_priv *priv);

static void spi_slave_time_complete(void *arg)
{
	struct spi_slave_time_priv *priv = arg;
	int ret;

	ret = priv->msg.status;
	if (ret)
		goto terminate;

	ret = spi_slave_time_submit(priv);
	if (ret)
		goto terminate;

	return;

terminate:
	dev_info(&priv->spi->dev, "Terminating\n");
	complete(&priv->finished);
}

static int spi_slave_time_submit(struct spi_slave_time_priv *priv)
{
	u32 rem_us;
	int ret;
	u64 ts;

	ts = local_clock();
	rem_us = do_div(ts, 1000000000) / 1000;

	priv->buf[0] = cpu_to_be32(ts);
	priv->buf[1] = cpu_to_be32(rem_us);

	spi_message_init_with_transfers(&priv->msg, &priv->xfer, 1);

	priv->msg.complete = spi_slave_time_complete;
	priv->msg.context = priv;

	ret = spi_async(priv->spi, &priv->msg);
	if (ret)
		dev_err(&priv->spi->dev, "spi_async() failed %d\n", ret);

	return ret;
}

static int spi_slave_time_probe(struct spi_device *spi)
{
	struct spi_slave_time_priv *priv;
	int ret;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	init_completion(&priv->finished);
	priv->xfer.tx_buf = priv->buf;
	priv->xfer.len = sizeof(priv->buf);

	ret = spi_slave_time_submit(priv);
	if (ret)
		return ret;

	spi_set_drvdata(spi, priv);
	return 0;
}

static void spi_slave_time_remove(struct spi_device *spi)
{
	struct spi_slave_time_priv *priv = spi_get_drvdata(spi);

	spi_slave_abort(spi);
	wait_for_completion(&priv->finished);
}

static struct spi_driver spi_slave_time_driver = {
	.driver = {
		.name	= "spi-slave-time",
	},
	.probe		= spi_slave_time_probe,
	.remove		= spi_slave_time_remove,
};
module_spi_driver(spi_slave_time_driver);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("SPI slave reporting uptime at previous SPI message");
MODULE_LICENSE("GPL v2");
