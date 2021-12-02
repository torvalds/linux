// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio I2C Bus Driver
 *
 * The Virtio I2C Specification:
 * https://raw.githubusercontent.com/oasis-tcs/virtio-spec/master/virtio-i2c.tex
 *
 * Copyright (c) 2021 Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_i2c.h>

/**
 * struct virtio_i2c - virtio I2C data
 * @vdev: virtio device for this controller
 * @adap: I2C adapter for this controller
 * @vq: the virtio virtqueue for communication
 */
struct virtio_i2c {
	struct virtio_device *vdev;
	struct i2c_adapter adap;
	struct virtqueue *vq;
};

/**
 * struct virtio_i2c_req - the virtio I2C request structure
 * @completion: completion of virtio I2C message
 * @out_hdr: the OUT header of the virtio I2C message
 * @buf: the buffer into which data is read, or from which it's written
 * @in_hdr: the IN header of the virtio I2C message
 */
struct virtio_i2c_req {
	struct completion completion;
	struct virtio_i2c_out_hdr out_hdr	____cacheline_aligned;
	uint8_t *buf				____cacheline_aligned;
	struct virtio_i2c_in_hdr in_hdr		____cacheline_aligned;
};

static void virtio_i2c_msg_done(struct virtqueue *vq)
{
	struct virtio_i2c_req *req;
	unsigned int len;

	while ((req = virtqueue_get_buf(vq, &len)))
		complete(&req->completion);
}

static int virtio_i2c_prepare_reqs(struct virtqueue *vq,
				   struct virtio_i2c_req *reqs,
				   struct i2c_msg *msgs, int num)
{
	struct scatterlist *sgs[3], out_hdr, msg_buf, in_hdr;
	int i;

	for (i = 0; i < num; i++) {
		int outcnt = 0, incnt = 0;

		init_completion(&reqs[i].completion);

		/*
		 * We don't support 0 length messages and so filter out
		 * 0 length transfers by using i2c_adapter_quirks.
		 */
		if (!msgs[i].len)
			break;

		/*
		 * Only 7-bit mode supported for this moment. For the address
		 * format, Please check the Virtio I2C Specification.
		 */
		reqs[i].out_hdr.addr = cpu_to_le16(msgs[i].addr << 1);

		if (i != num - 1)
			reqs[i].out_hdr.flags = cpu_to_le32(VIRTIO_I2C_FLAGS_FAIL_NEXT);

		sg_init_one(&out_hdr, &reqs[i].out_hdr, sizeof(reqs[i].out_hdr));
		sgs[outcnt++] = &out_hdr;

		reqs[i].buf = i2c_get_dma_safe_msg_buf(&msgs[i], 1);
		if (!reqs[i].buf)
			break;

		sg_init_one(&msg_buf, reqs[i].buf, msgs[i].len);

		if (msgs[i].flags & I2C_M_RD)
			sgs[outcnt + incnt++] = &msg_buf;
		else
			sgs[outcnt++] = &msg_buf;

		sg_init_one(&in_hdr, &reqs[i].in_hdr, sizeof(reqs[i].in_hdr));
		sgs[outcnt + incnt++] = &in_hdr;

		if (virtqueue_add_sgs(vq, sgs, outcnt, incnt, &reqs[i], GFP_KERNEL)) {
			i2c_put_dma_safe_msg_buf(reqs[i].buf, &msgs[i], false);
			break;
		}
	}

	return i;
}

static int virtio_i2c_complete_reqs(struct virtqueue *vq,
				    struct virtio_i2c_req *reqs,
				    struct i2c_msg *msgs, int num)
{
	bool failed = false;
	int i, j = 0;

	for (i = 0; i < num; i++) {
		struct virtio_i2c_req *req = &reqs[i];

		wait_for_completion(&req->completion);

		if (!failed && req->in_hdr.status != VIRTIO_I2C_MSG_OK)
			failed = true;

		i2c_put_dma_safe_msg_buf(reqs[i].buf, &msgs[i], !failed);

		if (!failed)
			j++;
	}

	return j;
}

static int virtio_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct virtio_i2c *vi = i2c_get_adapdata(adap);
	struct virtqueue *vq = vi->vq;
	struct virtio_i2c_req *reqs;
	int count;

	reqs = kcalloc(num, sizeof(*reqs), GFP_KERNEL);
	if (!reqs)
		return -ENOMEM;

	count = virtio_i2c_prepare_reqs(vq, reqs, msgs, num);
	if (!count)
		goto err_free;

	/*
	 * For the case where count < num, i.e. we weren't able to queue all the
	 * msgs, ideally we should abort right away and return early, but some
	 * of the messages are already sent to the remote I2C controller and the
	 * virtqueue will be left in undefined state in that case. We kick the
	 * remote here to clear the virtqueue, so we can try another set of
	 * messages later on.
	 */
	virtqueue_kick(vq);

	count = virtio_i2c_complete_reqs(vq, reqs, msgs, count);

err_free:
	kfree(reqs);
	return count;
}

static void virtio_i2c_del_vqs(struct virtio_device *vdev)
{
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

static int virtio_i2c_setup_vqs(struct virtio_i2c *vi)
{
	struct virtio_device *vdev = vi->vdev;

	vi->vq = virtio_find_single_vq(vdev, virtio_i2c_msg_done, "msg");
	return PTR_ERR_OR_ZERO(vi->vq);
}

static u32 virtio_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static struct i2c_algorithm virtio_algorithm = {
	.master_xfer = virtio_i2c_xfer,
	.functionality = virtio_i2c_func,
};

static const struct i2c_adapter_quirks virtio_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

static int virtio_i2c_probe(struct virtio_device *vdev)
{
	struct virtio_i2c *vi;
	int ret;

	vi = devm_kzalloc(&vdev->dev, sizeof(*vi), GFP_KERNEL);
	if (!vi)
		return -ENOMEM;

	vdev->priv = vi;
	vi->vdev = vdev;

	ret = virtio_i2c_setup_vqs(vi);
	if (ret)
		return ret;

	vi->adap.owner = THIS_MODULE;
	snprintf(vi->adap.name, sizeof(vi->adap.name),
		 "i2c_virtio at virtio bus %d", vdev->index);
	vi->adap.algo = &virtio_algorithm;
	vi->adap.quirks = &virtio_i2c_quirks;
	vi->adap.dev.parent = &vdev->dev;
	vi->adap.dev.of_node = vdev->dev.of_node;
	i2c_set_adapdata(&vi->adap, vi);

	/*
	 * Setup ACPI node for controlled devices which will be probed through
	 * ACPI.
	 */
	ACPI_COMPANION_SET(&vi->adap.dev, ACPI_COMPANION(vdev->dev.parent));

	ret = i2c_add_adapter(&vi->adap);
	if (ret)
		virtio_i2c_del_vqs(vdev);

	return ret;
}

static void virtio_i2c_remove(struct virtio_device *vdev)
{
	struct virtio_i2c *vi = vdev->priv;

	i2c_del_adapter(&vi->adap);
	virtio_i2c_del_vqs(vdev);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_I2C_ADAPTER, VIRTIO_DEV_ANY_ID },
	{}
};
MODULE_DEVICE_TABLE(virtio, id_table);

#ifdef CONFIG_PM_SLEEP
static int virtio_i2c_freeze(struct virtio_device *vdev)
{
	virtio_i2c_del_vqs(vdev);
	return 0;
}

static int virtio_i2c_restore(struct virtio_device *vdev)
{
	return virtio_i2c_setup_vqs(vdev->priv);
}
#endif

static struct virtio_driver virtio_i2c_driver = {
	.id_table	= id_table,
	.probe		= virtio_i2c_probe,
	.remove		= virtio_i2c_remove,
	.driver	= {
		.name	= "i2c_virtio",
	},
#ifdef CONFIG_PM_SLEEP
	.freeze = virtio_i2c_freeze,
	.restore = virtio_i2c_restore,
#endif
};
module_virtio_driver(virtio_i2c_driver);

MODULE_AUTHOR("Jie Deng <jie.deng@intel.com>");
MODULE_AUTHOR("Conghui Chen <conghui.chen@intel.com>");
MODULE_DESCRIPTION("Virtio i2c bus driver");
MODULE_LICENSE("GPL");
