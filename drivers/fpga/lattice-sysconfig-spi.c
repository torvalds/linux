// SPDX-License-Identifier: GPL-2.0
/*
 * Lattice FPGA programming over slave SPI sysCONFIG interface.
 */

#include <linux/spi/spi.h>

#include "lattice-sysconfig.h"

static const u32 ecp5_spi_max_speed_hz = 60000000;

static int sysconfig_spi_cmd_transfer(struct sysconfig_priv *priv,
				      const void *tx_buf, size_t tx_len,
				      void *rx_buf, size_t rx_len)
{
	struct spi_device *spi = to_spi_device(priv->dev);

	return spi_write_then_read(spi, tx_buf, tx_len, rx_buf, rx_len);
}

static int sysconfig_spi_bitstream_burst_init(struct sysconfig_priv *priv)
{
	const u8 lsc_bitstream_burst[] = SYSCONFIG_LSC_BITSTREAM_BURST;
	struct spi_device *spi = to_spi_device(priv->dev);
	struct spi_transfer xfer = {};
	struct spi_message msg;
	size_t buf_len;
	void *buf;
	int ret;

	buf_len = sizeof(lsc_bitstream_burst);

	buf = kmemdup(lsc_bitstream_burst, buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	xfer.len = buf_len;
	xfer.tx_buf = buf;
	xfer.cs_change = 1;

	spi_message_init_with_transfers(&msg, &xfer, 1);

	/*
	 * Lock SPI bus for exclusive usage until FPGA programming is done.
	 * SPI bus will be released in sysconfig_spi_bitstream_burst_complete().
	 */
	spi_bus_lock(spi->controller);

	ret = spi_sync_locked(spi, &msg);
	if (ret)
		spi_bus_unlock(spi->controller);

	kfree(buf);

	return ret;
}

static int sysconfig_spi_bitstream_burst_write(struct sysconfig_priv *priv,
					       const char *buf, size_t len)
{
	struct spi_device *spi = to_spi_device(priv->dev);
	struct spi_transfer xfer = {
		.tx_buf = buf,
		.len = len,
		.cs_change = 1,
	};
	struct spi_message msg;

	spi_message_init_with_transfers(&msg, &xfer, 1);

	return spi_sync_locked(spi, &msg);
}

static int sysconfig_spi_bitstream_burst_complete(struct sysconfig_priv *priv)
{
	struct spi_device *spi = to_spi_device(priv->dev);

	/* Bitstream burst write is done, release SPI bus */
	spi_bus_unlock(spi->controller);

	/* Toggle CS to finish bitstream write */
	return spi_write(spi, NULL, 0);
}

static int sysconfig_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *dev_id;
	struct device *dev = &spi->dev;
	struct sysconfig_priv *priv;
	const u32 *spi_max_speed;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spi_max_speed = device_get_match_data(dev);
	if (!spi_max_speed) {
		dev_id = spi_get_device_id(spi);
		if (!dev_id)
			return -ENODEV;

		spi_max_speed = (const u32 *)dev_id->driver_data;
	}

	if (!spi_max_speed)
		return -EINVAL;

	if (spi->max_speed_hz > *spi_max_speed) {
		dev_err(dev, "SPI speed %u is too high, maximum speed is %u\n",
			spi->max_speed_hz, *spi_max_speed);
		return -EINVAL;
	}

	priv->dev = dev;
	priv->command_transfer = sysconfig_spi_cmd_transfer;
	priv->bitstream_burst_write_init = sysconfig_spi_bitstream_burst_init;
	priv->bitstream_burst_write = sysconfig_spi_bitstream_burst_write;
	priv->bitstream_burst_write_complete = sysconfig_spi_bitstream_burst_complete;

	return sysconfig_probe(priv);
}

static const struct spi_device_id sysconfig_spi_ids[] = {
	{
		.name = "sysconfig-ecp5",
		.driver_data = (kernel_ulong_t)&ecp5_spi_max_speed_hz,
	}, {},
};
MODULE_DEVICE_TABLE(spi, sysconfig_spi_ids);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sysconfig_of_ids[] = {
	{
		.compatible = "lattice,sysconfig-ecp5",
		.data = &ecp5_spi_max_speed_hz,
	}, {},
};
MODULE_DEVICE_TABLE(of, sysconfig_of_ids);
#endif /* IS_ENABLED(CONFIG_OF) */

static struct spi_driver lattice_sysconfig_driver = {
	.probe = sysconfig_spi_probe,
	.id_table = sysconfig_spi_ids,
	.driver = {
		.name = "lattice_sysconfig_spi_fpga_mgr",
		.of_match_table = of_match_ptr(sysconfig_of_ids),
	},
};
module_spi_driver(lattice_sysconfig_driver);

MODULE_DESCRIPTION("Lattice sysCONFIG Slave SPI FPGA Manager");
MODULE_LICENSE("GPL");
