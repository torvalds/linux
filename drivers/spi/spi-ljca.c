// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel La Jolla Cove Adapter USB-SPI driver
 *
 * Copyright (c) 2023, Intel Corporation.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/usb/ljca.h>

#define LJCA_SPI_BUS_MAX_HZ		48000000

#define LJCA_SPI_BUF_SIZE		60u
#define LJCA_SPI_MAX_XFER_SIZE		\
	(LJCA_SPI_BUF_SIZE - sizeof(struct ljca_spi_xfer_packet))

#define LJCA_SPI_CLK_MODE_POLARITY	BIT(0)
#define LJCA_SPI_CLK_MODE_PHASE		BIT(1)

#define LJCA_SPI_XFER_INDICATOR_ID	GENMASK(5, 0)
#define LJCA_SPI_XFER_INDICATOR_CMPL	BIT(6)
#define LJCA_SPI_XFER_INDICATOR_INDEX	BIT(7)

/* SPI commands */
enum ljca_spi_cmd {
	LJCA_SPI_INIT = 1,
	LJCA_SPI_READ,
	LJCA_SPI_WRITE,
	LJCA_SPI_WRITEREAD,
	LJCA_SPI_DEINIT,
};

enum {
	LJCA_SPI_BUS_SPEED_24M,
	LJCA_SPI_BUS_SPEED_12M,
	LJCA_SPI_BUS_SPEED_8M,
	LJCA_SPI_BUS_SPEED_6M,
	LJCA_SPI_BUS_SPEED_4_8M, /*4.8MHz*/
	LJCA_SPI_BUS_SPEED_MIN = LJCA_SPI_BUS_SPEED_4_8M,
};

enum {
	LJCA_SPI_CLOCK_LOW_POLARITY,
	LJCA_SPI_CLOCK_HIGH_POLARITY,
};

enum {
	LJCA_SPI_CLOCK_FIRST_PHASE,
	LJCA_SPI_CLOCK_SECOND_PHASE,
};

struct ljca_spi_init_packet {
	u8 index;
	u8 speed;
	u8 mode;
} __packed;

struct ljca_spi_xfer_packet {
	u8 indicator;
	u8 len;
	u8 data[] __counted_by(len);
} __packed;

struct ljca_spi_dev {
	struct ljca_client *ljca;
	struct spi_controller *controller;
	struct ljca_spi_info *spi_info;
	u8 speed;
	u8 mode;

	u8 obuf[LJCA_SPI_BUF_SIZE];
	u8 ibuf[LJCA_SPI_BUF_SIZE];
};

static int ljca_spi_read_write(struct ljca_spi_dev *ljca_spi, const u8 *w_data,
			       u8 *r_data, int len, int id, int complete,
			       int cmd)
{
	struct ljca_spi_xfer_packet *w_packet =
			(struct ljca_spi_xfer_packet *)ljca_spi->obuf;
	struct ljca_spi_xfer_packet *r_packet =
			(struct ljca_spi_xfer_packet *)ljca_spi->ibuf;
	int ret;

	w_packet->indicator = FIELD_PREP(LJCA_SPI_XFER_INDICATOR_ID, id) |
			      FIELD_PREP(LJCA_SPI_XFER_INDICATOR_CMPL, complete) |
			      FIELD_PREP(LJCA_SPI_XFER_INDICATOR_INDEX,
					 ljca_spi->spi_info->id);

	if (cmd == LJCA_SPI_READ) {
		w_packet->len = sizeof(u16);
		*(__le16 *)&w_packet->data[0] = cpu_to_le16(len);
	} else {
		w_packet->len = len;
		memcpy(w_packet->data, w_data, len);
	}

	ret = ljca_transfer(ljca_spi->ljca, cmd, (u8 *)w_packet,
			    struct_size(w_packet, data, w_packet->len),
			    (u8 *)r_packet, LJCA_SPI_BUF_SIZE);
	if (ret < 0)
		return ret;
	else if (ret < sizeof(*r_packet) || r_packet->len <= 0)
		return -EIO;

	if (r_data)
		memcpy(r_data, r_packet->data, r_packet->len);

	return 0;
}

static int ljca_spi_init(struct ljca_spi_dev *ljca_spi, u8 div, u8 mode)
{
	struct ljca_spi_init_packet w_packet = {};
	int ret;

	if (ljca_spi->mode == mode && ljca_spi->speed == div)
		return 0;

	w_packet.index = ljca_spi->spi_info->id;
	w_packet.speed = div;
	w_packet.mode = FIELD_PREP(LJCA_SPI_CLK_MODE_POLARITY,
				   (mode & SPI_CPOL) ? LJCA_SPI_CLOCK_HIGH_POLARITY :
						       LJCA_SPI_CLOCK_LOW_POLARITY) |
			FIELD_PREP(LJCA_SPI_CLK_MODE_PHASE,
				   (mode & SPI_CPHA) ? LJCA_SPI_CLOCK_SECOND_PHASE :
						       LJCA_SPI_CLOCK_FIRST_PHASE);

	ret = ljca_transfer(ljca_spi->ljca, LJCA_SPI_INIT, (u8 *)&w_packet,
			    sizeof(w_packet), NULL, 0);
	if (ret < 0)
		return ret;

	ljca_spi->mode = mode;
	ljca_spi->speed = div;

	return 0;
}

static int ljca_spi_deinit(struct ljca_spi_dev *ljca_spi)
{
	struct ljca_spi_init_packet w_packet = {};
	int ret;

	w_packet.index = ljca_spi->spi_info->id;

	ret = ljca_transfer(ljca_spi->ljca, LJCA_SPI_DEINIT, (u8 *)&w_packet,
			    sizeof(w_packet), NULL, 0);

	return ret < 0 ? ret : 0;
}

static inline int ljca_spi_transfer(struct ljca_spi_dev *ljca_spi,
				    const u8 *tx_data, u8 *rx_data, u16 len)
{
	int complete, cur_len;
	int remaining = len;
	int cmd, ret, i;
	int offset = 0;

	if (tx_data && rx_data)
		cmd = LJCA_SPI_WRITEREAD;
	else if (tx_data)
		cmd = LJCA_SPI_WRITE;
	else if (rx_data)
		cmd = LJCA_SPI_READ;
	else
		return -EINVAL;

	for (i = 0; remaining > 0; i++) {
		cur_len = min_t(unsigned int, remaining, LJCA_SPI_MAX_XFER_SIZE);
		complete = (cur_len == remaining);

		ret = ljca_spi_read_write(ljca_spi,
					  tx_data ? tx_data + offset : NULL,
					  rx_data ? rx_data + offset : NULL,
					  cur_len, i, complete, cmd);
		if (ret)
			return ret;

		offset += cur_len;
		remaining -= cur_len;
	}

	return 0;
}

static int ljca_spi_transfer_one(struct spi_controller *controller,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	u8 div = DIV_ROUND_UP(controller->max_speed_hz, xfer->speed_hz) / 2 - 1;
	struct ljca_spi_dev *ljca_spi = spi_controller_get_devdata(controller);
	int ret;

	div = min_t(u8, LJCA_SPI_BUS_SPEED_MIN, div);

	ret = ljca_spi_init(ljca_spi, div, spi->mode);
	if (ret) {
		dev_err(&ljca_spi->ljca->auxdev.dev,
			"cannot initialize transfer ret %d\n", ret);
		return ret;
	}

	ret = ljca_spi_transfer(ljca_spi, xfer->tx_buf, xfer->rx_buf, xfer->len);
	if (ret)
		dev_err(&ljca_spi->ljca->auxdev.dev,
			"transfer failed len: %d\n", xfer->len);

	return ret;
}

static int ljca_spi_probe(struct auxiliary_device *auxdev,
			  const struct auxiliary_device_id *aux_dev_id)
{
	struct ljca_client *ljca = auxiliary_dev_to_ljca_client(auxdev);
	struct spi_controller *controller;
	struct ljca_spi_dev *ljca_spi;
	int ret;

	controller = devm_spi_alloc_host(&auxdev->dev, sizeof(*ljca_spi));
	if (!controller)
		return -ENOMEM;

	ljca_spi = spi_controller_get_devdata(controller);
	ljca_spi->ljca = ljca;
	ljca_spi->spi_info = dev_get_platdata(&auxdev->dev);
	ljca_spi->controller = controller;

	controller->bus_num = -1;
	controller->mode_bits = SPI_CPHA | SPI_CPOL;
	controller->transfer_one = ljca_spi_transfer_one;
	controller->auto_runtime_pm = false;
	controller->max_speed_hz = LJCA_SPI_BUS_MAX_HZ;

	device_set_node(&ljca_spi->controller->dev, dev_fwnode(&auxdev->dev));
	auxiliary_set_drvdata(auxdev, controller);

	ret = spi_register_controller(controller);
	if (ret)
		dev_err(&auxdev->dev, "Failed to register controller\n");

	return ret;
}

static void ljca_spi_dev_remove(struct auxiliary_device *auxdev)
{
	struct spi_controller *controller = auxiliary_get_drvdata(auxdev);
	struct ljca_spi_dev *ljca_spi = spi_controller_get_devdata(controller);

	spi_unregister_controller(controller);
	ljca_spi_deinit(ljca_spi);
}

static int ljca_spi_dev_suspend(struct device *dev)
{
	struct spi_controller *controller = dev_get_drvdata(dev);

	return spi_controller_suspend(controller);
}

static int ljca_spi_dev_resume(struct device *dev)
{
	struct spi_controller *controller = dev_get_drvdata(dev);

	return spi_controller_resume(controller);
}

static const struct dev_pm_ops ljca_spi_pm = {
	SYSTEM_SLEEP_PM_OPS(ljca_spi_dev_suspend, ljca_spi_dev_resume)
};

static const struct auxiliary_device_id ljca_spi_id_table[] = {
	{ "usb_ljca.ljca-spi", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(auxiliary, ljca_spi_id_table);

static struct auxiliary_driver ljca_spi_driver = {
	.driver.pm	= &ljca_spi_pm,
	.probe		= ljca_spi_probe,
	.remove		= ljca_spi_dev_remove,
	.id_table	= ljca_spi_id_table,
};
module_auxiliary_driver(ljca_spi_driver);

MODULE_AUTHOR("Wentong Wu <wentong.wu@intel.com>");
MODULE_AUTHOR("Zhifeng Wang <zhifeng.wang@intel.com>");
MODULE_AUTHOR("Lixu Zhang <lixu.zhang@intel.com>");
MODULE_DESCRIPTION("Intel La Jolla Cove Adapter USB-SPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(LJCA);
