#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "kxsd9.h"

#define KXSD9_READ(a) (0x80 | (a))
#define KXSD9_WRITE(a) (a)

static int kxsd9_spi_readreg(struct kxsd9_transport *tr, u8 address)
{
	struct spi_device *spi = tr->trdev;

	return spi_w8r8(spi, KXSD9_READ(address));
}

static int kxsd9_spi_writereg(struct kxsd9_transport *tr, u8 address, u8 val)
{
	struct spi_device *spi = tr->trdev;

	tr->tx[0] = KXSD9_WRITE(address),
	tr->tx[1] = val;
	return spi_write(spi, tr->tx, 2);
}

static int kxsd9_spi_write2(struct kxsd9_transport *tr, u8 b1, u8 b2)
{
	struct spi_device *spi = tr->trdev;

	tr->tx[0] = b1;
	tr->tx[1] = b2;
	return spi_write(spi, tr->tx, 2);
}

static int kxsd9_spi_readval(struct kxsd9_transport *tr, u8 address)
{
	struct spi_device *spi = tr->trdev;
	struct spi_transfer xfers[] = {
		{
			.bits_per_word = 8,
			.len = 1,
			.delay_usecs = 200,
			.tx_buf = tr->tx,
		}, {
			.bits_per_word = 8,
			.len = 2,
			.rx_buf = tr->rx,
		},
	};
	int ret;

	tr->tx[0] = KXSD9_READ(address);
	ret = spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
	if (!ret)
		ret = (((u16)(tr->rx[0])) << 8) | (tr->rx[1]);
	return ret;
}

static int kxsd9_spi_probe(struct spi_device *spi)
{
	struct kxsd9_transport *transport;
	int ret;

	transport = devm_kzalloc(&spi->dev, sizeof(*transport), GFP_KERNEL);
	if (!transport)
		return -ENOMEM;

	transport->trdev = spi;
	transport->readreg = kxsd9_spi_readreg;
	transport->writereg = kxsd9_spi_writereg;
	transport->write2 = kxsd9_spi_write2;
	transport->readval = kxsd9_spi_readval;
	spi->mode = SPI_MODE_0;
	spi_setup(spi);

	ret = kxsd9_common_probe(&spi->dev,
				 transport,
				 spi_get_device_id(spi)->name);
	if (ret)
		return ret;

	return 0;
}

static int kxsd9_spi_remove(struct spi_device *spi)
{
	return kxsd9_common_remove(&spi->dev);
}

static const struct spi_device_id kxsd9_spi_id[] = {
	{"kxsd9", 0},
	{ },
};
MODULE_DEVICE_TABLE(spi, kxsd9_spi_id);

static struct spi_driver kxsd9_spi_driver = {
	.driver = {
		.name = "kxsd9",
	},
	.probe = kxsd9_spi_probe,
	.remove = kxsd9_spi_remove,
	.id_table = kxsd9_spi_id,
};
module_spi_driver(kxsd9_spi_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Kionix KXSD9 SPI driver");
MODULE_LICENSE("GPL v2");
