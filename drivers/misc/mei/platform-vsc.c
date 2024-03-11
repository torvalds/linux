// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Intel Corporation.
 * Intel Visual Sensing Controller Interface Linux driver
 */

#include <linux/align.h>
#include <linux/cache.h>
#include <linux/cleanup.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/mei.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include <asm-generic/bug.h>
#include <asm-generic/unaligned.h>

#include "mei_dev.h"
#include "vsc-tp.h"

#define MEI_VSC_DRV_NAME		"intel_vsc"

#define MEI_VSC_MAX_MSG_SIZE		512

#define MEI_VSC_POLL_DELAY_US		(50 * USEC_PER_MSEC)
#define MEI_VSC_POLL_TIMEOUT_US		(200 * USEC_PER_MSEC)

#define mei_dev_to_vsc_hw(dev)		((struct mei_vsc_hw *)((dev)->hw))

struct mei_vsc_host_timestamp {
	u64 realtime;
	u64 boottime;
};

struct mei_vsc_hw {
	struct vsc_tp *tp;

	bool fw_ready;
	bool host_ready;

	atomic_t write_lock_cnt;

	u32 rx_len;
	u32 rx_hdr;

	/* buffer for tx */
	char tx_buf[MEI_VSC_MAX_MSG_SIZE + sizeof(struct mei_msg_hdr)] ____cacheline_aligned;
	/* buffer for rx */
	char rx_buf[MEI_VSC_MAX_MSG_SIZE + sizeof(struct mei_msg_hdr)] ____cacheline_aligned;
};

static int mei_vsc_read_helper(struct mei_vsc_hw *hw, u8 *buf,
			       u32 max_len)
{
	struct mei_vsc_host_timestamp ts = {
		.realtime = ktime_to_ns(ktime_get_real()),
		.boottime = ktime_to_ns(ktime_get_boottime()),
	};

	return vsc_tp_xfer(hw->tp, VSC_TP_CMD_READ, &ts, sizeof(ts),
			   buf, max_len);
}

static int mei_vsc_write_helper(struct mei_vsc_hw *hw, u8 *buf, u32 len)
{
	u8 status;

	return vsc_tp_xfer(hw->tp, VSC_TP_CMD_WRITE, buf, len, &status,
			   sizeof(status));
}

static int mei_vsc_fw_status(struct mei_device *mei_dev,
			     struct mei_fw_status *fw_status)
{
	if (!fw_status)
		return -EINVAL;

	fw_status->count = 0;

	return 0;
}

static inline enum mei_pg_state mei_vsc_pg_state(struct mei_device *mei_dev)
{
	return MEI_PG_OFF;
}

static void mei_vsc_intr_enable(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	vsc_tp_intr_enable(hw->tp);
}

static void mei_vsc_intr_disable(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	vsc_tp_intr_disable(hw->tp);
}

/* mei framework requires this ops */
static void mei_vsc_intr_clear(struct mei_device *mei_dev)
{
}

/* wait for pending irq handler */
static void mei_vsc_synchronize_irq(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	vsc_tp_intr_synchronize(hw->tp);
}

static int mei_vsc_hw_config(struct mei_device *mei_dev)
{
	return 0;
}

static bool mei_vsc_host_is_ready(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	return hw->host_ready;
}

static bool mei_vsc_hw_is_ready(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	return hw->fw_ready;
}

static int mei_vsc_hw_start(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);
	int ret, rlen;
	u8 buf;

	hw->host_ready = true;

	vsc_tp_intr_enable(hw->tp);

	ret = read_poll_timeout(mei_vsc_read_helper, rlen,
				rlen >= 0, MEI_VSC_POLL_DELAY_US,
				MEI_VSC_POLL_TIMEOUT_US, true,
				hw, &buf, sizeof(buf));
	if (ret) {
		dev_err(mei_dev->dev, "wait fw ready failed: %d\n", ret);
		return ret;
	}

	hw->fw_ready = true;

	return 0;
}

static bool mei_vsc_hbuf_is_ready(struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	return atomic_read(&hw->write_lock_cnt) == 0;
}

static int mei_vsc_hbuf_empty_slots(struct mei_device *mei_dev)
{
	return MEI_VSC_MAX_MSG_SIZE / MEI_SLOT_SIZE;
}

static u32 mei_vsc_hbuf_depth(const struct mei_device *mei_dev)
{
	return MEI_VSC_MAX_MSG_SIZE / MEI_SLOT_SIZE;
}

static int mei_vsc_write(struct mei_device *mei_dev,
			 const void *hdr, size_t hdr_len,
			 const void *data, size_t data_len)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);
	char *buf = hw->tx_buf;
	int ret;

	if (WARN_ON(!hdr || !IS_ALIGNED(hdr_len, 4)))
		return -EINVAL;

	if (!data || data_len > MEI_VSC_MAX_MSG_SIZE)
		return -EINVAL;

	atomic_inc(&hw->write_lock_cnt);

	memcpy(buf, hdr, hdr_len);
	memcpy(buf + hdr_len, data, data_len);

	ret = mei_vsc_write_helper(hw, buf, hdr_len + data_len);

	atomic_dec_if_positive(&hw->write_lock_cnt);

	return ret < 0 ? ret : 0;
}

static inline u32 mei_vsc_read(const struct mei_device *mei_dev)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);
	int ret;

	ret = mei_vsc_read_helper(hw, hw->rx_buf, sizeof(hw->rx_buf));
	if (ret < 0 || ret < sizeof(u32))
		return 0;
	hw->rx_len = ret;

	hw->rx_hdr = get_unaligned_le32(hw->rx_buf);

	return hw->rx_hdr;
}

static int mei_vsc_count_full_read_slots(struct mei_device *mei_dev)
{
	return MEI_VSC_MAX_MSG_SIZE / MEI_SLOT_SIZE;
}

static int mei_vsc_read_slots(struct mei_device *mei_dev, unsigned char *buf,
			      unsigned long len)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);
	struct mei_msg_hdr *hdr;

	hdr = (struct mei_msg_hdr *)&hw->rx_hdr;
	if (len != hdr->length || hdr->length + sizeof(*hdr) != hw->rx_len)
		return -EINVAL;

	memcpy(buf, hw->rx_buf + sizeof(*hdr), len);

	return 0;
}

static bool mei_vsc_pg_in_transition(struct mei_device *mei_dev)
{
	return mei_dev->pg_event >= MEI_PG_EVENT_WAIT &&
	       mei_dev->pg_event <= MEI_PG_EVENT_INTR_WAIT;
}

static bool mei_vsc_pg_is_enabled(struct mei_device *mei_dev)
{
	return false;
}

static int mei_vsc_hw_reset(struct mei_device *mei_dev, bool intr_enable)
{
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);

	vsc_tp_reset(hw->tp);

	vsc_tp_intr_disable(hw->tp);

	return vsc_tp_init(hw->tp, mei_dev->dev);
}

static const struct mei_hw_ops mei_vsc_hw_ops = {
	.fw_status = mei_vsc_fw_status,
	.pg_state = mei_vsc_pg_state,

	.host_is_ready = mei_vsc_host_is_ready,
	.hw_is_ready = mei_vsc_hw_is_ready,
	.hw_reset = mei_vsc_hw_reset,
	.hw_config = mei_vsc_hw_config,
	.hw_start = mei_vsc_hw_start,

	.pg_in_transition = mei_vsc_pg_in_transition,
	.pg_is_enabled = mei_vsc_pg_is_enabled,

	.intr_clear = mei_vsc_intr_clear,
	.intr_enable = mei_vsc_intr_enable,
	.intr_disable = mei_vsc_intr_disable,
	.synchronize_irq = mei_vsc_synchronize_irq,

	.hbuf_free_slots = mei_vsc_hbuf_empty_slots,
	.hbuf_is_ready = mei_vsc_hbuf_is_ready,
	.hbuf_depth = mei_vsc_hbuf_depth,
	.write = mei_vsc_write,

	.rdbuf_full_slots = mei_vsc_count_full_read_slots,
	.read_hdr = mei_vsc_read,
	.read = mei_vsc_read_slots,
};

static void mei_vsc_event_cb(void *context)
{
	struct mei_device *mei_dev = context;
	struct mei_vsc_hw *hw = mei_dev_to_vsc_hw(mei_dev);
	struct list_head cmpl_list;
	s32 slots;
	int ret;

	if (mei_dev->dev_state == MEI_DEV_RESETTING ||
	    mei_dev->dev_state == MEI_DEV_INITIALIZING)
		return;

	INIT_LIST_HEAD(&cmpl_list);

	guard(mutex)(&mei_dev->device_lock);

	while (vsc_tp_need_read(hw->tp)) {
		/* check slots available for reading */
		slots = mei_count_full_read_slots(mei_dev);

		ret = mei_irq_read_handler(mei_dev, &cmpl_list, &slots);
		if (ret) {
			if (ret != -ENODATA) {
				if (mei_dev->dev_state != MEI_DEV_RESETTING &&
				    mei_dev->dev_state != MEI_DEV_POWER_DOWN)
					schedule_work(&mei_dev->reset_work);
			}

			return;
		}
	}

	mei_dev->hbuf_is_ready = mei_hbuf_is_ready(mei_dev);
	ret = mei_irq_write_handler(mei_dev, &cmpl_list);
	if (ret)
		dev_err(mei_dev->dev, "dispatch write request failed: %d\n", ret);

	mei_dev->hbuf_is_ready = mei_hbuf_is_ready(mei_dev);
	mei_irq_compl_handler(mei_dev, &cmpl_list);
}

static int mei_vsc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mei_device *mei_dev;
	struct mei_vsc_hw *hw;
	struct vsc_tp *tp;
	int ret;

	tp = *(struct vsc_tp **)dev_get_platdata(dev);
	if (!tp)
		return dev_err_probe(dev, -ENODEV, "no platform data\n");

	mei_dev = devm_kzalloc(dev, size_add(sizeof(*mei_dev), sizeof(*hw)),
			       GFP_KERNEL);
	if (!mei_dev)
		return -ENOMEM;

	mei_device_init(mei_dev, dev, false, &mei_vsc_hw_ops);
	mei_dev->fw_f_fw_ver_supported = 0;
	mei_dev->kind = "ivsc";

	hw = mei_dev_to_vsc_hw(mei_dev);
	atomic_set(&hw->write_lock_cnt, 0);
	hw->tp = tp;

	platform_set_drvdata(pdev, mei_dev);

	vsc_tp_register_event_cb(tp, mei_vsc_event_cb, mei_dev);

	ret = mei_start(mei_dev);
	if (ret) {
		dev_err_probe(dev, ret, "init hw failed\n");
		goto err_cancel;
	}

	ret = mei_register(mei_dev, dev);
	if (ret)
		goto err_stop;

	pm_runtime_enable(mei_dev->dev);

	return 0;

err_stop:
	mei_stop(mei_dev);

err_cancel:
	mei_cancel_work(mei_dev);

	mei_disable_interrupts(mei_dev);

	return ret;
}

static int mei_vsc_remove(struct platform_device *pdev)
{
	struct mei_device *mei_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(mei_dev->dev);

	mei_stop(mei_dev);

	mei_disable_interrupts(mei_dev);

	mei_deregister(mei_dev);

	return 0;
}

static int mei_vsc_suspend(struct device *dev)
{
	struct mei_device *mei_dev = dev_get_drvdata(dev);

	mei_stop(mei_dev);

	return 0;
}

static int mei_vsc_resume(struct device *dev)
{
	struct mei_device *mei_dev = dev_get_drvdata(dev);
	int ret;

	ret = mei_restart(mei_dev);
	if (ret)
		return ret;

	/* start timer if stopped in suspend */
	schedule_delayed_work(&mei_dev->timer_work, HZ);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mei_vsc_pm_ops, mei_vsc_suspend, mei_vsc_resume);

static const struct platform_device_id mei_vsc_id_table[] = {
	{ MEI_VSC_DRV_NAME },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mei_vsc_id_table);

static struct platform_driver mei_vsc_drv = {
	.probe = mei_vsc_probe,
	.remove = mei_vsc_remove,
	.id_table = mei_vsc_id_table,
	.driver = {
		.name = MEI_VSC_DRV_NAME,
		.pm = &mei_vsc_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(mei_vsc_drv);

MODULE_AUTHOR("Wentong Wu <wentong.wu@intel.com>");
MODULE_AUTHOR("Zhifeng Wang <zhifeng.wang@intel.com>");
MODULE_DESCRIPTION("Intel Visual Sensing Controller Interface");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(VSC_TP);
