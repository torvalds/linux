// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/scatterlist.h>
#include <linux/idr.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/virtio.h>
#include <uapi/linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/spi/spi.h>

/* Virtio ID of SPI: 0xC009 */
#define VIRTIO_ID_SPI	                      49161

#define VIRTIO_CONFIG_SPI_BUS_NUMBER          0
#define VIRTIO_CONFIG_SPI_CS_MAX_NUMBER       4

#define VIRTIO_SPI_MSG_OK                     0

/**
 * the structure define the transfer configuration:
 *
 *  mode: how data is clocked out and in
 *  freq: transfer clock rate
 *  slave_id: chip select index the transfer used
 *  bits_per_word: transfer word size
 *  word_delay_usecs: how long to wait between words within one transfer
 *  cs_change: deselect device before starting the next transfer
 */
struct spi_transfer_head {
	u32 mode;
	u32 freq;
	u8 slave_id;
	u8 bits_per_word;
	u8 word_delay_usecs;
	u8 cs_change;
};

/**
 * the structure define the transfer result:
 *
 *  result: return value from backend
 */
struct spi_transfer_end {
	u8 result;
};

/**
 * the structure sent to the backend, including 2 parts:
 *  -- head: the configuration of the transfer
 *  -- end: the transfer result set by the backend
 *
 *  note: don't need allocate transfer buffer for the request struct.
 *        virtio-spi based on spidev driver, when open the spidev device,
 *        the driver will allocate the tx_buf and rx_buf for spidev device.
 *        For more details, please refer to the spidev_open function in spidev.c
 */
struct virtio_spi_req {
	struct spi_transfer_head head;
	struct spi_transfer_end  end;
};

/**
 * struct virtio_spi - virtio spi device
 * @spi: spi controller
 * @vdev: the virtio device
 * @spi_req: description of the fromat of transfer data
 * @vq: spi virtqueue
 */
struct virtio_spi {
	struct spi_master *spi;
	struct virtio_device *vdev;
	struct virtio_spi_req spi_req;
	struct virtqueue *vq;
	wait_queue_head_t inq;
};

/* virtqueue incoming data interrupt IRQ */
static void virtspi_vq_isr(struct virtqueue *vq)
{
	struct virtio_spi *vspi = vq->vdev->priv;

	wake_up(&vspi->inq);
}

static int virtspi_init_vqs(struct virtio_spi *vspi)
{
	struct virtqueue *vqs[1];
	vq_callback_t *cbs[] = { virtspi_vq_isr };
	static const char * const names[] = { "virtspi_vq_isr" };
	int err;

	err = virtio_find_vqs(vspi->vdev, 1, vqs, cbs, names, NULL);
	if (err)
		return err;
	vspi->vq = vqs[0];

	return 0;
}

static void virtspi_del_vqs(struct virtio_spi *vspi)
{
	vspi->vdev->config->del_vqs(vspi->vdev);
}

/**
 *  transfer one message to the backend
 */
static int virtio_spi_transfer_one(struct spi_master *spi, struct spi_device *slv,
				struct spi_transfer *xfer)
{
	struct virtio_spi *vspi = spi_controller_get_devdata(spi);
	struct virtio_spi_req *spi_req = &vspi->spi_req;

	struct scatterlist outhdr, tx_bufhdr, rx_bufhdr, inhdr, *sgs[4];
	unsigned int num_out = 0, num_in = 0, len;
	int err = 0;

	struct virtqueue *vq = vspi->vq;

	if ((xfer->tx_buf == NULL) && (xfer->rx_buf == NULL)) {
		dev_err(&vspi->vdev->dev, "Invalid transfer buffer.\n");
		return -EINVAL;
	}

	if (xfer->len < 1) {
		dev_err(&vspi->vdev->dev, "Invalid transfer length.\n");
		return -EINVAL;
	}

	spi_req->head.slave_id = slv->chip_select;
	spi_req->head.mode = slv->mode;
	spi_req->head.freq = xfer->speed_hz;

	if (xfer->bits_per_word == 0)
		spi_req->head.bits_per_word = slv->bits_per_word;
	else
		spi_req->head.bits_per_word = xfer->bits_per_word;

	spi_req->head.cs_change = xfer->cs_change;

	/* init the head queue */
	sg_init_one(&outhdr, &spi_req->head, sizeof(spi_req->head));
	sgs[num_out++] = &outhdr;

	if (xfer->tx_buf != NULL) {
		/* init the tx_buf queue */
		sg_init_one(&tx_bufhdr, xfer->tx_buf, xfer->len);
		sgs[num_out++] = &tx_bufhdr;
	}

	if (xfer->rx_buf != NULL) {
		/* init the rx_buf queue */
		sg_init_one(&rx_bufhdr, xfer->rx_buf, xfer->len);
		sgs[num_out + num_in++] = &rx_bufhdr;
	}

	/* init the result queue */
	sg_init_one(&inhdr, &spi_req->end, sizeof(spi_req->end));
	sgs[num_out + num_in++] = &inhdr;

	/* call the virtqueue function */
	err = virtqueue_add_sgs(vq, sgs, num_out, num_in, spi_req, GFP_KERNEL);
	if (err)
		return -EIO;

	/* Tell Host to go! */
	err = virtqueue_kick(vq);
	if (!err)
		return -EIO;

	wait_event(vspi->inq, virtqueue_get_buf(vq, &len));

	if (spi_req->end.result != VIRTIO_SPI_MSG_OK)
		err = -EIO;
	else
		err = 0;

	return err;
}

/**
 *	used to allocate and register the spi controller
 */
static int virtspi_init_hw(struct virtio_device *vdev,
				struct virtio_spi *vspi)
{
	int err;

	/* allocate the spi controller */
	vspi->spi = __spi_alloc_controller(&vdev->dev, 0, 0);
	if (!vspi->spi)
		return -ENOMEM;

	spi_controller_set_devdata(vspi->spi, vspi);

	vspi->spi->dev.of_node = vdev->dev.parent->of_node;

	/* read the bus number from the config space with offset 0 */
	vspi->spi->bus_num = virtio_cread32(vdev, VIRTIO_CONFIG_SPI_BUS_NUMBER);

	/* read the max chip select number from the config space with offset 4 */
	vspi->spi->num_chipselect = virtio_cread32(vdev, VIRTIO_CONFIG_SPI_CS_MAX_NUMBER);

	vspi->spi->mode_bits = (SPI_CPHA | SPI_CPOL | SPI_CS_HIGH
				| SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP
				| SPI_NO_CS | SPI_READY | SPI_TX_DUAL
				| SPI_TX_QUAD | SPI_RX_DUAL | SPI_RX_QUAD);
	vspi->spi->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);

	vspi->spi->transfer_one = virtio_spi_transfer_one;
	vspi->spi->auto_runtime_pm = false;

	/* register the spi controller */
	err = spi_register_controller(vspi->spi);
	if (err)
		return err;

	return 0;
}

static int virtspi_probe(struct virtio_device *vdev)
{
	struct virtio_spi *vspi;
	int err = 0;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	vspi = kzalloc(sizeof(*vspi), GFP_KERNEL);
	if (!vspi)
		return -ENOMEM;

	vspi->vdev = vdev;
	vdev->priv = vspi;
	init_waitqueue_head(&vspi->inq);

	err = virtspi_init_vqs(vspi);
	if (err)
		goto err_init_vq;

	virtio_device_ready(vdev);

	virtqueue_enable_cb(vspi->vq);

	err = virtspi_init_hw(vdev, vspi);
	if (err)
		goto err_init_hw;

	return 0;

err_init_hw:
	virtspi_del_vqs(vspi);
err_init_vq:
	kfree(vspi);
	return err;
}

static void virtspi_remove(struct virtio_device *vdev)
{
	struct virtio_spi *vspi = vdev->priv;

	spi_unregister_controller(vspi->spi);

	vdev->config->reset(vdev);

	virtspi_del_vqs(vspi);
	kfree(vspi);
}

static unsigned int features[] = {
	/* none */
};
static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SPI, VIRTIO_DEV_ANY_ID },
	{ },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_spi_driver = {
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.feature_table		= features,
	.feature_table_size	= ARRAY_SIZE(features),
	.id_table		= id_table,
	.probe			= virtspi_probe,
	.remove			= virtspi_remove,
};

module_virtio_driver(virtio_spi_driver);

MODULE_DESCRIPTION("Virtio spi frontend driver");
MODULE_LICENSE("GPL");
