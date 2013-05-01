/*
 * STMicroelectronics sensors spi library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors_spi.h>


#define ST_SENSORS_SPI_MULTIREAD	0xc0
#define ST_SENSORS_SPI_READ		0x80

static unsigned int st_sensors_spi_get_irq(struct iio_dev *indio_dev)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	return to_spi_device(sdata->dev)->irq;
}

static int st_sensors_spi_read(struct st_sensor_transfer_buffer *tb,
	struct device *dev, u8 reg_addr, int len, u8 *data, bool multiread_bit)
{
	struct spi_message msg;
	int err;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = tb->tx_buf,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = tb->rx_buf,
			.bits_per_word = 8,
			.len = len,
		}
	};

	mutex_lock(&tb->buf_lock);
	if ((multiread_bit) && (len > 1))
		tb->tx_buf[0] = reg_addr | ST_SENSORS_SPI_MULTIREAD;
	else
		tb->tx_buf[0] = reg_addr | ST_SENSORS_SPI_READ;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	err = spi_sync(to_spi_device(dev), &msg);
	if (err)
		goto acc_spi_read_error;

	memcpy(data, tb->rx_buf, len*sizeof(u8));
	mutex_unlock(&tb->buf_lock);
	return len;

acc_spi_read_error:
	mutex_unlock(&tb->buf_lock);
	return err;
}

static int st_sensors_spi_read_byte(struct st_sensor_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 *res_byte)
{
	return st_sensors_spi_read(tb, dev, reg_addr, 1, res_byte, false);
}

static int st_sensors_spi_read_multiple_byte(
	struct st_sensor_transfer_buffer *tb, struct device *dev,
			u8 reg_addr, int len, u8 *data, bool multiread_bit)
{
	return st_sensors_spi_read(tb, dev, reg_addr, len, data, multiread_bit);
}

static int st_sensors_spi_write_byte(struct st_sensor_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 data)
{
	struct spi_message msg;
	int err;

	struct spi_transfer xfers = {
		.tx_buf = tb->tx_buf,
		.bits_per_word = 8,
		.len = 2,
	};

	mutex_lock(&tb->buf_lock);
	tb->tx_buf[0] = reg_addr;
	tb->tx_buf[1] = data;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers, &msg);
	err = spi_sync(to_spi_device(dev), &msg);
	mutex_unlock(&tb->buf_lock);

	return err;
}

static const struct st_sensor_transfer_function st_sensors_tf_spi = {
	.read_byte = st_sensors_spi_read_byte,
	.write_byte = st_sensors_spi_write_byte,
	.read_multiple_byte = st_sensors_spi_read_multiple_byte,
};

void st_sensors_spi_configure(struct iio_dev *indio_dev,
			struct spi_device *spi, struct st_sensor_data *sdata)
{
	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi->modalias;

	sdata->tf = &st_sensors_tf_spi;
	sdata->get_irq_data_ready = st_sensors_spi_get_irq;
}
EXPORT_SYMBOL(st_sensors_spi_configure);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors spi driver");
MODULE_LICENSE("GPL v2");
