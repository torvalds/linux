// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI bus driver for the Virtio SPI controller
 * Copyright (C) 2023 OpenSynergy GmbH
 * Copyright (C) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/stddef.h>
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_spi.h>

#define VIRTIO_SPI_MODE_MASK \
	(SPI_MODE_X_MASK | SPI_CS_HIGH | SPI_LSB_FIRST)

struct virtio_spi_req {
	struct completion completion;
	const u8 *tx_buf;
	u8 *rx_buf;
	struct spi_transfer_head transfer_head	____cacheline_aligned;
	struct spi_transfer_result result;
};

struct virtio_spi_priv {
	/* The virtio device we're associated with */
	struct virtio_device *vdev;
	/* Pointer to the virtqueue */
	struct virtqueue *vq;
	/* Copy of config space mode_func_supported */
	u32 mode_func_supported;
	/* Copy of config space max_freq_hz */
	u32 max_freq_hz;
};

static void virtio_spi_msg_done(struct virtqueue *vq)
{
	struct virtio_spi_req *req;
	unsigned int len;

	while ((req = virtqueue_get_buf(vq, &len)))
		complete(&req->completion);
}

/*
 * virtio_spi_set_delays - Set delay parameters for SPI transfer
 *
 * This function sets various delay parameters for SPI transfer,
 * including delay after CS asserted, timing intervals between
 * adjacent words within a transfer, delay before and after CS
 * deasserted. It converts these delay parameters to nanoseconds
 * using spi_delay_to_ns and stores the results in spi_transfer_head
 * structure.
 * If the conversion fails, the function logs a warning message and
 * returns an error code.
 *       .   .      .    .    .   .   .   .   .   .
 * Delay + A +      + B  +    + C + D + E + F + A +
 *       .   .      .    .    .   .   .   .   .   .
 *    ___.   .      .    .    .   .   .___.___.   .
 * CS#   |___.______.____.____.___.___|   .   |___._____________
 *       .   .      .    .    .   .   .   .   .   .
 *       .   .      .    .    .   .   .   .   .   .
 * SCLK__.___.___NNN_____NNN__.___.___.___.___.___.___NNN_______
 *
 * NOTE: 1st transfer has two words, the delay between these two words are
 * 'B' in the diagram.
 *
 * A => struct spi_device -> cs_setup
 * B => max{struct spi_transfer -> word_delay, struct spi_device -> word_delay}
 *   Note: spi_device and spi_transfer both have word_delay, Linux
 *         choose the bigger one, refer to _spi_xfer_word_delay_update function
 * C => struct spi_transfer -> delay
 * D => struct spi_device -> cs_hold
 * E => struct spi_device -> cs_inactive
 * F => struct spi_transfer -> cs_change_delay
 *
 * So the corresponding relationship:
 * A   <===> cs_setup_ns (after CS asserted)
 * B   <===> word_delay_ns (delay between adjacent words within a transfer)
 * C+D <===> cs_delay_hold_ns (before CS deasserted)
 * E+F <===> cs_change_delay_inactive_ns (after CS deasserted, these two
 * values are also recommended in the Linux driver to be added up)
 */
static int virtio_spi_set_delays(struct spi_transfer_head *th,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	int cs_setup;
	int cs_word_delay_xfer;
	int cs_word_delay_spi;
	int delay;
	int cs_hold;
	int cs_inactive;
	int cs_change_delay;

	cs_setup = spi_delay_to_ns(&spi->cs_setup, xfer);
	if (cs_setup < 0) {
		dev_warn(&spi->dev, "Cannot convert cs_setup\n");
		return cs_setup;
	}
	th->cs_setup_ns = cpu_to_le32(cs_setup);

	cs_word_delay_xfer = spi_delay_to_ns(&xfer->word_delay, xfer);
	if (cs_word_delay_xfer < 0) {
		dev_warn(&spi->dev, "Cannot convert cs_word_delay_xfer\n");
		return cs_word_delay_xfer;
	}
	cs_word_delay_spi = spi_delay_to_ns(&spi->word_delay, xfer);
	if (cs_word_delay_spi < 0) {
		dev_warn(&spi->dev, "Cannot convert cs_word_delay_spi\n");
		return cs_word_delay_spi;
	}

	th->word_delay_ns = cpu_to_le32(max(cs_word_delay_spi, cs_word_delay_xfer));

	delay = spi_delay_to_ns(&xfer->delay, xfer);
	if (delay < 0) {
		dev_warn(&spi->dev, "Cannot convert delay\n");
		return delay;
	}
	cs_hold = spi_delay_to_ns(&spi->cs_hold, xfer);
	if (cs_hold < 0) {
		dev_warn(&spi->dev, "Cannot convert cs_hold\n");
		return cs_hold;
	}
	th->cs_delay_hold_ns = cpu_to_le32(delay + cs_hold);

	cs_inactive = spi_delay_to_ns(&spi->cs_inactive, xfer);
	if (cs_inactive < 0) {
		dev_warn(&spi->dev, "Cannot convert cs_inactive\n");
		return cs_inactive;
	}
	cs_change_delay = spi_delay_to_ns(&xfer->cs_change_delay, xfer);
	if (cs_change_delay < 0) {
		dev_warn(&spi->dev, "Cannot convert cs_change_delay\n");
		return cs_change_delay;
	}
	th->cs_change_delay_inactive_ns =
		cpu_to_le32(cs_inactive + cs_change_delay);

	return 0;
}

static int virtio_spi_transfer_one(struct spi_controller *ctrl,
				   struct spi_device *spi,
				   struct spi_transfer *xfer)
{
	struct virtio_spi_priv *priv = spi_controller_get_devdata(ctrl);
	struct virtio_spi_req *spi_req __free(kfree) = NULL;
	struct spi_transfer_head *th;
	struct scatterlist sg_out_head, sg_out_payload;
	struct scatterlist sg_in_result, sg_in_payload;
	struct scatterlist *sgs[4];
	unsigned int outcnt = 0;
	unsigned int incnt = 0;
	int ret;

	spi_req = kzalloc(sizeof(*spi_req), GFP_KERNEL);
	if (!spi_req)
		return -ENOMEM;

	init_completion(&spi_req->completion);

	th = &spi_req->transfer_head;

	/* Fill struct spi_transfer_head */
	th->chip_select_id = spi_get_chipselect(spi, 0);
	th->bits_per_word = spi->bits_per_word;
	th->cs_change = xfer->cs_change;
	th->tx_nbits = xfer->tx_nbits;
	th->rx_nbits = xfer->rx_nbits;
	th->reserved[0] = 0;
	th->reserved[1] = 0;
	th->reserved[2] = 0;

	static_assert(VIRTIO_SPI_CPHA == SPI_CPHA,
		      "VIRTIO_SPI_CPHA must match SPI_CPHA");
	static_assert(VIRTIO_SPI_CPOL == SPI_CPOL,
		      "VIRTIO_SPI_CPOL must match SPI_CPOL");
	static_assert(VIRTIO_SPI_CS_HIGH == SPI_CS_HIGH,
		      "VIRTIO_SPI_CS_HIGH must match SPI_CS_HIGH");
	static_assert(VIRTIO_SPI_MODE_LSB_FIRST == SPI_LSB_FIRST,
		      "VIRTIO_SPI_MODE_LSB_FIRST must match SPI_LSB_FIRST");

	th->mode = cpu_to_le32(spi->mode & VIRTIO_SPI_MODE_MASK);
	if (spi->mode & SPI_LOOP)
		th->mode |= cpu_to_le32(VIRTIO_SPI_MODE_LOOP);

	th->freq = cpu_to_le32(xfer->speed_hz);

	ret = virtio_spi_set_delays(th, spi, xfer);
	if (ret)
		goto msg_done;

	/* Set buffers */
	spi_req->tx_buf = xfer->tx_buf;
	spi_req->rx_buf = xfer->rx_buf;

	/* Prepare sending of virtio message */
	init_completion(&spi_req->completion);

	sg_init_one(&sg_out_head, th, sizeof(*th));
	sgs[outcnt] = &sg_out_head;
	outcnt++;

	if (spi_req->tx_buf) {
		sg_init_one(&sg_out_payload, spi_req->tx_buf, xfer->len);
		sgs[outcnt] = &sg_out_payload;
		outcnt++;
	}

	if (spi_req->rx_buf) {
		sg_init_one(&sg_in_payload, spi_req->rx_buf, xfer->len);
		sgs[outcnt] = &sg_in_payload;
		incnt++;
	}

	sg_init_one(&sg_in_result, &spi_req->result,
		    sizeof(struct spi_transfer_result));
	sgs[outcnt + incnt] = &sg_in_result;
	incnt++;

	ret = virtqueue_add_sgs(priv->vq, sgs, outcnt, incnt, spi_req,
				GFP_KERNEL);
	if (ret)
		goto msg_done;

	/* Simple implementation: There can be only one transfer in flight */
	virtqueue_kick(priv->vq);

	wait_for_completion(&spi_req->completion);

	/* Read result from message and translate return code */
	switch (spi_req->result.result) {
	case VIRTIO_SPI_TRANS_OK:
		break;
	case VIRTIO_SPI_PARAM_ERR:
		ret = -EINVAL;
		break;
	case VIRTIO_SPI_TRANS_ERR:
		ret = -EIO;
		break;
	default:
		ret = -EIO;
		break;
	}

msg_done:
	if (ret)
		ctrl->cur_msg->status = ret;

	return ret;
}

static void virtio_spi_read_config(struct virtio_device *vdev)
{
	struct spi_controller *ctrl = dev_get_drvdata(&vdev->dev);
	struct virtio_spi_priv *priv = vdev->priv;
	u8 cs_max_number;
	u8 tx_nbits_supported;
	u8 rx_nbits_supported;

	cs_max_number = virtio_cread8(vdev, offsetof(struct virtio_spi_config,
						     cs_max_number));
	ctrl->num_chipselect = cs_max_number;

	/* Set the mode bits which are understood by this driver */
	priv->mode_func_supported =
		virtio_cread32(vdev, offsetof(struct virtio_spi_config,
					      mode_func_supported));
	ctrl->mode_bits = priv->mode_func_supported &
			  (VIRTIO_SPI_CS_HIGH | VIRTIO_SPI_MODE_LSB_FIRST);
	if (priv->mode_func_supported & VIRTIO_SPI_MF_SUPPORT_CPHA_1)
		ctrl->mode_bits |= VIRTIO_SPI_CPHA;
	if (priv->mode_func_supported & VIRTIO_SPI_MF_SUPPORT_CPOL_1)
		ctrl->mode_bits |= VIRTIO_SPI_CPOL;
	if (priv->mode_func_supported & VIRTIO_SPI_MF_SUPPORT_LSB_FIRST)
		ctrl->mode_bits |= SPI_LSB_FIRST;
	if (priv->mode_func_supported & VIRTIO_SPI_MF_SUPPORT_LOOPBACK)
		ctrl->mode_bits |= SPI_LOOP;
	tx_nbits_supported =
		virtio_cread8(vdev, offsetof(struct virtio_spi_config,
					     tx_nbits_supported));
	if (tx_nbits_supported & VIRTIO_SPI_RX_TX_SUPPORT_DUAL)
		ctrl->mode_bits |= SPI_TX_DUAL;
	if (tx_nbits_supported & VIRTIO_SPI_RX_TX_SUPPORT_QUAD)
		ctrl->mode_bits |= SPI_TX_QUAD;
	if (tx_nbits_supported & VIRTIO_SPI_RX_TX_SUPPORT_OCTAL)
		ctrl->mode_bits |= SPI_TX_OCTAL;
	rx_nbits_supported =
		virtio_cread8(vdev, offsetof(struct virtio_spi_config,
					     rx_nbits_supported));
	if (rx_nbits_supported & VIRTIO_SPI_RX_TX_SUPPORT_DUAL)
		ctrl->mode_bits |= SPI_RX_DUAL;
	if (rx_nbits_supported & VIRTIO_SPI_RX_TX_SUPPORT_QUAD)
		ctrl->mode_bits |= SPI_RX_QUAD;
	if (rx_nbits_supported & VIRTIO_SPI_RX_TX_SUPPORT_OCTAL)
		ctrl->mode_bits |= SPI_RX_OCTAL;

	ctrl->bits_per_word_mask =
		virtio_cread32(vdev, offsetof(struct virtio_spi_config,
					      bits_per_word_mask));

	priv->max_freq_hz =
		virtio_cread32(vdev, offsetof(struct virtio_spi_config,
					      max_freq_hz));
}

static int virtio_spi_find_vqs(struct virtio_spi_priv *priv)
{
	struct virtqueue *vq;

	vq = virtio_find_single_vq(priv->vdev, virtio_spi_msg_done, "spi-rq");
	if (IS_ERR(vq))
		return PTR_ERR(vq);
	priv->vq = vq;
	return 0;
}

/* Function must not be called before virtio_spi_find_vqs() has been run */
static void virtio_spi_del_vq(void *data)
{
	struct virtio_device *vdev = data;

	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
}

static int virtio_spi_probe(struct virtio_device *vdev)
{
	struct virtio_spi_priv *priv;
	struct spi_controller *ctrl;
	int ret;

	ctrl = devm_spi_alloc_host(&vdev->dev, sizeof(*priv));
	if (!ctrl)
		return -ENOMEM;

	priv = spi_controller_get_devdata(ctrl);
	priv->vdev = vdev;
	vdev->priv = priv;

	device_set_node(&ctrl->dev, dev_fwnode(&vdev->dev));

	dev_set_drvdata(&vdev->dev, ctrl);

	virtio_spi_read_config(vdev);

	ctrl->transfer_one = virtio_spi_transfer_one;

	ret = virtio_spi_find_vqs(priv);
	if (ret)
		return dev_err_probe(&vdev->dev, ret, "Cannot setup virtqueues\n");

	/* Register cleanup for virtqueues using devm */
	ret = devm_add_action_or_reset(&vdev->dev, virtio_spi_del_vq, vdev);
	if (ret)
		return dev_err_probe(&vdev->dev, ret, "Cannot register virtqueue cleanup\n");

	/* Use devm version to register controller */
	ret = devm_spi_register_controller(&vdev->dev, ctrl);
	if (ret)
		return dev_err_probe(&vdev->dev, ret, "Cannot register controller\n");

	return 0;
}

static int virtio_spi_freeze(struct device *dev)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct virtio_device *vdev = dev_to_virtio(dev);
	int ret;

	ret = spi_controller_suspend(ctrl);
	if (ret) {
		dev_warn(dev, "cannot suspend controller (%d)\n", ret);
		return ret;
	}

	virtio_spi_del_vq(vdev);
	return 0;
}

static int virtio_spi_restore(struct device *dev)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct virtio_device *vdev = dev_to_virtio(dev);
	int ret;

	ret = virtio_spi_find_vqs(vdev->priv);
	if (ret) {
		dev_err(dev, "problem starting vqueue (%d)\n", ret);
		return ret;
	}

	ret = spi_controller_resume(ctrl);
	if (ret)
		dev_err(dev, "problem resuming controller (%d)\n", ret);

	return ret;
}

static struct virtio_device_id virtio_spi_id_table[] = {
	{ VIRTIO_ID_SPI, VIRTIO_DEV_ANY_ID },
	{}
};
MODULE_DEVICE_TABLE(virtio, virtio_spi_id_table);

static const struct dev_pm_ops virtio_spi_pm_ops = {
	.freeze = pm_sleep_ptr(virtio_spi_freeze),
	.restore = pm_sleep_ptr(virtio_spi_restore),
};

static struct virtio_driver virtio_spi_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.pm = &virtio_spi_pm_ops,
	},
	.id_table = virtio_spi_id_table,
	.probe = virtio_spi_probe,
};
module_virtio_driver(virtio_spi_driver);

MODULE_AUTHOR("OpenSynergy GmbH");
MODULE_AUTHOR("Haixu Cui <quic_haixcui@quicinc.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtio SPI bus driver");
