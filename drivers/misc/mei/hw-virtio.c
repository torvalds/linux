// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2018-2020, Intel Corporation.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/atomic.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

#define MEI_VIRTIO_RPM_TIMEOUT 500
/* ACRN virtio device types */
#ifndef VIRTIO_ID_MEI
#define VIRTIO_ID_MEI 0xFFFE /* virtio mei */
#endif

/**
 * struct mei_virtio_cfg - settings passed from the virtio backend
 * @buf_depth: read buffer depth in slots (4bytes)
 * @hw_ready: hw is ready for operation
 * @host_reset: synchronize reset with virtio backend
 * @reserved: reserved for alignment
 * @fw_status: FW status
 */
struct mei_virtio_cfg {
	u32 buf_depth;
	u8 hw_ready;
	u8 host_reset;
	u8 reserved[2];
	u32 fw_status[MEI_FW_STATUS_MAX];
} __packed;

struct mei_virtio_hw {
	struct mei_device mdev;
	char name[32];

	struct virtqueue *in;
	struct virtqueue *out;

	bool host_ready;
	struct work_struct intr_handler;

	u32 *recv_buf;
	u8 recv_rdy;
	size_t recv_sz;
	u32 recv_idx;
	u32 recv_len;

	/* send buffer */
	atomic_t hbuf_ready;
	const void *send_hdr;
	const void *send_buf;

	struct mei_virtio_cfg cfg;
};

#define to_virtio_hw(_dev) container_of(_dev, struct mei_virtio_hw, mdev)

/**
 * mei_virtio_fw_status() - read status register of mei
 * @dev: mei device
 * @fw_status: fw status register values
 *
 * Return: always 0
 */
static int mei_virtio_fw_status(struct mei_device *dev,
				struct mei_fw_status *fw_status)
{
	struct virtio_device *vdev = dev_to_virtio(dev->dev);

	fw_status->count = MEI_FW_STATUS_MAX;
	virtio_cread_bytes(vdev, offsetof(struct mei_virtio_cfg, fw_status),
			   fw_status->status, sizeof(fw_status->status));
	return 0;
}

/**
 * mei_virtio_pg_state() - translate internal pg state
 *   to the mei power gating state
 *   There is no power management in ACRN mode always return OFF
 * @dev: mei device
 *
 * Return:
 * * MEI_PG_OFF - if aliveness is on (always)
 * * MEI_PG_ON  - (never)
 */
static inline enum mei_pg_state mei_virtio_pg_state(struct mei_device *dev)
{
	return MEI_PG_OFF;
}

/**
 * mei_virtio_hw_config() - configure hw dependent settings
 *
 * @dev: mei device
 *
 * Return: always 0
 */
static int mei_virtio_hw_config(struct mei_device *dev)
{
	return 0;
}

/**
 * mei_virtio_hbuf_empty_slots() - counts write empty slots.
 * @dev: the device structure
 *
 * Return: always return frontend buf size if buffer is ready, 0 otherwise
 */
static int mei_virtio_hbuf_empty_slots(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	return (atomic_read(&hw->hbuf_ready) == 1) ? hw->cfg.buf_depth : 0;
}

/**
 * mei_virtio_hbuf_is_ready() - checks if write buffer is ready
 * @dev: the device structure
 *
 * Return: true if hbuf is ready
 */
static bool mei_virtio_hbuf_is_ready(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	return atomic_read(&hw->hbuf_ready) == 1;
}

/**
 * mei_virtio_hbuf_max_depth() - returns depth of FE write buffer.
 * @dev: the device structure
 *
 * Return: size of frontend write buffer in bytes
 */
static u32 mei_virtio_hbuf_depth(const struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	return hw->cfg.buf_depth;
}

/**
 * mei_virtio_intr_clear() - clear and stop interrupts
 * @dev: the device structure
 */
static void mei_virtio_intr_clear(struct mei_device *dev)
{
	/*
	 * In our virtio solution, there are two types of interrupts,
	 * vq interrupt and config change interrupt.
	 *   1) start/reset rely on virtio config changed interrupt;
	 *   2) send/recv rely on virtio virtqueue interrupts.
	 * They are all virtual interrupts. So, we don't have corresponding
	 * operation to do here.
	 */
}

/**
 * mei_virtio_intr_enable() - enables mei BE virtqueues callbacks
 * @dev: the device structure
 */
static void mei_virtio_intr_enable(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	struct virtio_device *vdev = dev_to_virtio(dev->dev);

	virtio_config_enable(vdev);

	virtqueue_enable_cb(hw->in);
	virtqueue_enable_cb(hw->out);
}

/**
 * mei_virtio_intr_disable() - disables mei BE virtqueues callbacks
 *
 * @dev: the device structure
 */
static void mei_virtio_intr_disable(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	struct virtio_device *vdev = dev_to_virtio(dev->dev);

	virtio_config_disable(vdev);

	virtqueue_disable_cb(hw->in);
	virtqueue_disable_cb(hw->out);
}

/**
 * mei_virtio_synchronize_irq() - wait for pending IRQ handlers for all
 *     virtqueue
 * @dev: the device structure
 */
static void mei_virtio_synchronize_irq(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	/*
	 * Now, all IRQ handlers are converted to workqueue.
	 * Change synchronize irq to flush this work.
	 */
	flush_work(&hw->intr_handler);
}

static void mei_virtio_free_outbufs(struct mei_virtio_hw *hw)
{
	kfree(hw->send_hdr);
	kfree(hw->send_buf);
	hw->send_hdr = NULL;
	hw->send_buf = NULL;
}

/**
 * mei_virtio_write_message() - writes a message to mei virtio back-end service.
 * @dev: the device structure
 * @hdr: mei header of message
 * @hdr_len: header length
 * @data: message payload will be written
 * @data_len: message payload length
 *
 * Return:
 * *  0: on success
 * * -EIO: if write has failed
 * * -ENOMEM: on memory allocation failure
 */
static int mei_virtio_write_message(struct mei_device *dev,
				    const void *hdr, size_t hdr_len,
				    const void *data, size_t data_len)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	struct scatterlist sg[2];
	const void *hbuf, *dbuf;
	int ret;

	if (WARN_ON(!atomic_add_unless(&hw->hbuf_ready, -1, 0)))
		return -EIO;

	hbuf = kmemdup(hdr, hdr_len, GFP_KERNEL);
	hw->send_hdr = hbuf;

	dbuf = kmemdup(data, data_len, GFP_KERNEL);
	hw->send_buf = dbuf;

	if (!hbuf || !dbuf) {
		ret = -ENOMEM;
		goto fail;
	}

	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], hbuf, hdr_len);
	sg_set_buf(&sg[1], dbuf, data_len);

	ret = virtqueue_add_outbuf(hw->out, sg, 2, hw, GFP_KERNEL);
	if (ret) {
		dev_err(dev->dev, "failed to add outbuf\n");
		goto fail;
	}

	virtqueue_kick(hw->out);
	return 0;
fail:

	mei_virtio_free_outbufs(hw);

	return ret;
}

/**
 * mei_virtio_count_full_read_slots() - counts read full slots.
 * @dev: the device structure
 *
 * Return: -EOVERFLOW if overflow, otherwise filled slots count
 */
static int mei_virtio_count_full_read_slots(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	if (hw->recv_idx > hw->recv_len)
		return -EOVERFLOW;

	return hw->recv_len - hw->recv_idx;
}

/**
 * mei_virtio_read_hdr() - Reads 32bit dword from mei virtio receive buffer
 *
 * @dev: the device structure
 *
 * Return: 32bit dword of receive buffer (u32)
 */
static inline u32 mei_virtio_read_hdr(const struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	WARN_ON(hw->cfg.buf_depth < hw->recv_idx + 1);

	return hw->recv_buf[hw->recv_idx++];
}

static int mei_virtio_read(struct mei_device *dev, unsigned char *buffer,
			   unsigned long len)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	u32 slots = mei_data2slots(len);

	if (WARN_ON(hw->cfg.buf_depth < hw->recv_idx + slots))
		return -EOVERFLOW;

	/*
	 * Assumption: There is only one MEI message in recv_buf each time.
	 * Backend service need follow this rule too.
	 */
	memcpy(buffer, hw->recv_buf + hw->recv_idx, len);
	hw->recv_idx += slots;

	return 0;
}

static bool mei_virtio_pg_is_enabled(struct mei_device *dev)
{
	return false;
}

static bool mei_virtio_pg_in_transition(struct mei_device *dev)
{
	return false;
}

static void mei_virtio_add_recv_buf(struct mei_virtio_hw *hw)
{
	struct scatterlist sg;

	if (hw->recv_rdy) /* not needed */
		return;

	/* refill the recv_buf to IN virtqueue to get next message */
	sg_init_one(&sg, hw->recv_buf, mei_slots2data(hw->cfg.buf_depth));
	hw->recv_len = 0;
	hw->recv_idx = 0;
	hw->recv_rdy = 1;
	virtqueue_add_inbuf(hw->in, &sg, 1, hw->recv_buf, GFP_KERNEL);
	virtqueue_kick(hw->in);
}

/**
 * mei_virtio_hw_is_ready() - check whether the BE(hw) has turned ready
 * @dev: mei device
 * Return: bool
 */
static bool mei_virtio_hw_is_ready(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	struct virtio_device *vdev = dev_to_virtio(dev->dev);

	virtio_cread(vdev, struct mei_virtio_cfg,
		     hw_ready, &hw->cfg.hw_ready);

	dev_dbg(dev->dev, "hw ready %d\n", hw->cfg.hw_ready);

	return hw->cfg.hw_ready;
}

/**
 * mei_virtio_hw_reset - resets virtio hw.
 *
 * @dev: the device structure
 * @intr_enable: virtio use data/config callbacks
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_virtio_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	struct virtio_device *vdev = dev_to_virtio(dev->dev);

	dev_dbg(dev->dev, "hw reset\n");

	dev->recvd_hw_ready = false;
	hw->host_ready = false;
	atomic_set(&hw->hbuf_ready, 0);
	hw->recv_len = 0;
	hw->recv_idx = 0;

	hw->cfg.host_reset = 1;
	virtio_cwrite(vdev, struct mei_virtio_cfg,
		      host_reset, &hw->cfg.host_reset);

	mei_virtio_hw_is_ready(dev);

	if (intr_enable)
		mei_virtio_intr_enable(dev);

	return 0;
}

/**
 * mei_virtio_hw_reset_release() - release device from the reset
 * @dev: the device structure
 */
static void mei_virtio_hw_reset_release(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	struct virtio_device *vdev = dev_to_virtio(dev->dev);

	dev_dbg(dev->dev, "hw reset release\n");
	hw->cfg.host_reset = 0;
	virtio_cwrite(vdev, struct mei_virtio_cfg,
		      host_reset, &hw->cfg.host_reset);
}

/**
 * mei_virtio_hw_ready_wait() - wait until the virtio(hw) has turned ready
 *  or timeout is reached
 * @dev: mei device
 *
 * Return: 0 on success, error otherwise
 */
static int mei_virtio_hw_ready_wait(struct mei_device *dev)
{
	mutex_unlock(&dev->device_lock);
	wait_event_timeout(dev->wait_hw_ready,
			   dev->recvd_hw_ready,
			   mei_secs_to_jiffies(MEI_HW_READY_TIMEOUT));
	mutex_lock(&dev->device_lock);
	if (!dev->recvd_hw_ready) {
		dev_err(dev->dev, "wait hw ready failed\n");
		return -ETIMEDOUT;
	}

	dev->recvd_hw_ready = false;
	return 0;
}

/**
 * mei_virtio_hw_start() - hw start routine
 * @dev: mei device
 *
 * Return: 0 on success, error otherwise
 */
static int mei_virtio_hw_start(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);
	int ret;

	dev_dbg(dev->dev, "hw start\n");
	mei_virtio_hw_reset_release(dev);

	ret = mei_virtio_hw_ready_wait(dev);
	if (ret)
		return ret;

	mei_virtio_add_recv_buf(hw);
	atomic_set(&hw->hbuf_ready, 1);
	dev_dbg(dev->dev, "hw is ready\n");
	hw->host_ready = true;

	return 0;
}

/**
 * mei_virtio_host_is_ready() - check whether the FE has turned ready
 * @dev: mei device
 *
 * Return: bool
 */
static bool mei_virtio_host_is_ready(struct mei_device *dev)
{
	struct mei_virtio_hw *hw = to_virtio_hw(dev);

	dev_dbg(dev->dev, "host ready %d\n", hw->host_ready);

	return hw->host_ready;
}

/**
 * mei_virtio_data_in() - The callback of recv virtqueue of virtio mei
 * @vq: receiving virtqueue
 */
static void mei_virtio_data_in(struct virtqueue *vq)
{
	struct mei_virtio_hw *hw = vq->vdev->priv;

	/* disable interrupts (enabled again from in the interrupt worker) */
	virtqueue_disable_cb(hw->in);

	schedule_work(&hw->intr_handler);
}

/**
 * mei_virtio_data_out() - The callback of send virtqueue of virtio mei
 * @vq: transmitting virtqueue
 */
static void mei_virtio_data_out(struct virtqueue *vq)
{
	struct mei_virtio_hw *hw = vq->vdev->priv;

	schedule_work(&hw->intr_handler);
}

static void mei_virtio_intr_handler(struct work_struct *work)
{
	struct mei_virtio_hw *hw =
		container_of(work, struct mei_virtio_hw, intr_handler);
	struct mei_device *dev = &hw->mdev;
	LIST_HEAD(complete_list);
	s32 slots;
	int rets = 0;
	void *data;
	unsigned int len;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state == MEI_DEV_DISABLED) {
		dev_warn(dev->dev, "Interrupt in disabled state.\n");
		mei_virtio_intr_disable(dev);
		goto end;
	}

	/* check if ME wants a reset */
	if (!mei_hw_is_ready(dev) && dev->dev_state != MEI_DEV_RESETTING) {
		dev_warn(dev->dev, "BE service not ready: resetting.\n");
		schedule_work(&dev->reset_work);
		goto end;
	}

	/* check if we need to start the dev */
	if (!mei_host_is_ready(dev)) {
		if (mei_hw_is_ready(dev)) {
			dev_dbg(dev->dev, "we need to start the dev.\n");
			dev->recvd_hw_ready = true;
			wake_up(&dev->wait_hw_ready);
		} else {
			dev_warn(dev->dev, "Spurious Interrupt\n");
		}
		goto end;
	}

	/* read */
	if (hw->recv_rdy) {
		data = virtqueue_get_buf(hw->in, &len);
		if (!data || !len) {
			dev_dbg(dev->dev, "No data %d", len);
		} else {
			dev_dbg(dev->dev, "data_in %d\n", len);
			WARN_ON(data != hw->recv_buf);
			hw->recv_len = mei_data2slots(len);
			hw->recv_rdy = 0;
		}
	}

	/* write */
	if (!atomic_read(&hw->hbuf_ready)) {
		if (!virtqueue_get_buf(hw->out, &len)) {
			dev_warn(dev->dev, "Failed to getbuf\n");
		} else {
			mei_virtio_free_outbufs(hw);
			atomic_inc(&hw->hbuf_ready);
		}
	}

	/* check slots available for reading */
	slots = mei_count_full_read_slots(dev);
	while (slots > 0) {
		dev_dbg(dev->dev, "slots to read = %08x\n", slots);
		rets = mei_irq_read_handler(dev, &complete_list, &slots);

		if (rets &&
		    (dev->dev_state != MEI_DEV_RESETTING &&
		     dev->dev_state != MEI_DEV_POWER_DOWN)) {
			dev_err(dev->dev, "mei_irq_read_handler ret = %d.\n",
				rets);
			schedule_work(&dev->reset_work);
			goto end;
		}
	}

	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);

	mei_irq_write_handler(dev, &complete_list);

	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);

	mei_irq_compl_handler(dev, &complete_list);

	mei_virtio_add_recv_buf(hw);

end:
	if (dev->dev_state != MEI_DEV_DISABLED) {
		if (!virtqueue_enable_cb(hw->in))
			schedule_work(&hw->intr_handler);
	}

	mutex_unlock(&dev->device_lock);
}

static void mei_virtio_config_changed(struct virtio_device *vdev)
{
	struct mei_virtio_hw *hw = vdev->priv;
	struct mei_device *dev = &hw->mdev;

	virtio_cread(vdev, struct mei_virtio_cfg,
		     hw_ready, &hw->cfg.hw_ready);

	if (dev->dev_state == MEI_DEV_DISABLED) {
		dev_dbg(dev->dev, "disabled state don't start\n");
		return;
	}

	/* Run intr handler once to handle reset notify */
	schedule_work(&hw->intr_handler);
}

static void mei_virtio_remove_vqs(struct virtio_device *vdev)
{
	struct mei_virtio_hw *hw = vdev->priv;

	virtqueue_detach_unused_buf(hw->in);
	hw->recv_len = 0;
	hw->recv_idx = 0;
	hw->recv_rdy = 0;

	virtqueue_detach_unused_buf(hw->out);

	mei_virtio_free_outbufs(hw);

	vdev->config->del_vqs(vdev);
}

/*
 * There are two virtqueues, one is for send and another is for recv.
 */
static int mei_virtio_init_vqs(struct mei_virtio_hw *hw,
			       struct virtio_device *vdev)
{
	struct virtqueue *vqs[2];

	vq_callback_t *cbs[] = {
		mei_virtio_data_in,
		mei_virtio_data_out,
	};
	static const char * const names[] = {
		"in",
		"out",
	};
	int ret;

	ret = virtio_find_vqs(vdev, 2, vqs, cbs, names, NULL);
	if (ret)
		return ret;

	hw->in = vqs[0];
	hw->out = vqs[1];

	return 0;
}

static const struct mei_hw_ops mei_virtio_ops = {
	.fw_status = mei_virtio_fw_status,
	.pg_state  = mei_virtio_pg_state,

	.host_is_ready = mei_virtio_host_is_ready,

	.hw_is_ready = mei_virtio_hw_is_ready,
	.hw_reset = mei_virtio_hw_reset,
	.hw_config = mei_virtio_hw_config,
	.hw_start = mei_virtio_hw_start,

	.pg_in_transition = mei_virtio_pg_in_transition,
	.pg_is_enabled = mei_virtio_pg_is_enabled,

	.intr_clear = mei_virtio_intr_clear,
	.intr_enable = mei_virtio_intr_enable,
	.intr_disable = mei_virtio_intr_disable,
	.synchronize_irq = mei_virtio_synchronize_irq,

	.hbuf_free_slots = mei_virtio_hbuf_empty_slots,
	.hbuf_is_ready = mei_virtio_hbuf_is_ready,
	.hbuf_depth = mei_virtio_hbuf_depth,

	.write = mei_virtio_write_message,

	.rdbuf_full_slots = mei_virtio_count_full_read_slots,
	.read_hdr = mei_virtio_read_hdr,
	.read = mei_virtio_read,
};

static int mei_virtio_probe(struct virtio_device *vdev)
{
	struct mei_virtio_hw *hw;
	int ret;

	hw = devm_kzalloc(&vdev->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	vdev->priv = hw;

	INIT_WORK(&hw->intr_handler, mei_virtio_intr_handler);

	ret = mei_virtio_init_vqs(hw, vdev);
	if (ret)
		goto vqs_failed;

	virtio_cread(vdev, struct mei_virtio_cfg,
		     buf_depth, &hw->cfg.buf_depth);

	hw->recv_buf = kzalloc(mei_slots2data(hw->cfg.buf_depth), GFP_KERNEL);
	if (!hw->recv_buf) {
		ret = -ENOMEM;
		goto hbuf_failed;
	}
	atomic_set(&hw->hbuf_ready, 0);

	virtio_device_ready(vdev);

	mei_device_init(&hw->mdev, &vdev->dev, &mei_virtio_ops);

	pm_runtime_get_noresume(&vdev->dev);
	pm_runtime_set_active(&vdev->dev);
	pm_runtime_enable(&vdev->dev);

	ret = mei_start(&hw->mdev);
	if (ret)
		goto mei_start_failed;

	pm_runtime_set_autosuspend_delay(&vdev->dev, MEI_VIRTIO_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(&vdev->dev);

	ret = mei_register(&hw->mdev, &vdev->dev);
	if (ret)
		goto mei_failed;

	pm_runtime_put(&vdev->dev);

	return 0;

mei_failed:
	mei_stop(&hw->mdev);
mei_start_failed:
	mei_cancel_work(&hw->mdev);
	mei_disable_interrupts(&hw->mdev);
	kfree(hw->recv_buf);
hbuf_failed:
	vdev->config->del_vqs(vdev);
vqs_failed:
	return ret;
}

static int __maybe_unused mei_virtio_pm_runtime_idle(struct device *device)
{
	struct virtio_device *vdev = dev_to_virtio(device);
	struct mei_virtio_hw *hw = vdev->priv;

	dev_dbg(&vdev->dev, "rpm: mei_virtio : runtime_idle\n");

	if (!hw)
		return -ENODEV;

	if (mei_write_is_idle(&hw->mdev))
		pm_runtime_autosuspend(device);

	return -EBUSY;
}

static int __maybe_unused mei_virtio_pm_runtime_suspend(struct device *device)
{
	return 0;
}

static int __maybe_unused mei_virtio_pm_runtime_resume(struct device *device)
{
	return 0;
}

static int __maybe_unused mei_virtio_freeze(struct virtio_device *vdev)
{
	struct mei_virtio_hw *hw = vdev->priv;

	dev_dbg(&vdev->dev, "freeze\n");

	if (!hw)
		return -ENODEV;

	mei_stop(&hw->mdev);
	mei_disable_interrupts(&hw->mdev);
	cancel_work_sync(&hw->intr_handler);
	vdev->config->reset(vdev);
	mei_virtio_remove_vqs(vdev);

	return 0;
}

static int __maybe_unused mei_virtio_restore(struct virtio_device *vdev)
{
	struct mei_virtio_hw *hw = vdev->priv;
	int ret;

	dev_dbg(&vdev->dev, "restore\n");

	if (!hw)
		return -ENODEV;

	ret = mei_virtio_init_vqs(hw, vdev);
	if (ret)
		return ret;

	virtio_device_ready(vdev);

	ret = mei_restart(&hw->mdev);
	if (ret)
		return ret;

	/* Start timer if stopped in suspend */
	schedule_delayed_work(&hw->mdev.timer_work, HZ);

	return 0;
}

static const struct dev_pm_ops mei_virtio_pm_ops = {
	SET_RUNTIME_PM_OPS(mei_virtio_pm_runtime_suspend,
			   mei_virtio_pm_runtime_resume,
			   mei_virtio_pm_runtime_idle)
};

static void mei_virtio_remove(struct virtio_device *vdev)
{
	struct mei_virtio_hw *hw = vdev->priv;

	mei_stop(&hw->mdev);
	mei_disable_interrupts(&hw->mdev);
	cancel_work_sync(&hw->intr_handler);
	mei_deregister(&hw->mdev);
	vdev->config->reset(vdev);
	mei_virtio_remove_vqs(vdev);
	kfree(hw->recv_buf);
	pm_runtime_disable(&vdev->dev);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_MEI, VIRTIO_DEV_ANY_ID },
	{ }
};

static struct virtio_driver mei_virtio_driver = {
	.id_table = id_table,
	.probe = mei_virtio_probe,
	.remove = mei_virtio_remove,
	.config_changed = mei_virtio_config_changed,
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		.pm = &mei_virtio_pm_ops,
	},
#ifdef CONFIG_PM_SLEEP
	.freeze = mei_virtio_freeze,
	.restore = mei_virtio_restore,
#endif
};

module_virtio_driver(mei_virtio_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio MEI frontend driver");
MODULE_LICENSE("GPL v2");
