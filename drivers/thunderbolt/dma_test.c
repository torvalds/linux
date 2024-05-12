// SPDX-License-Identifier: GPL-2.0
/*
 * DMA traffic test driver
 *
 * Copyright (C) 2020, Intel Corporation
 * Authors: Isaac Hazan <isaac.hazan@intel.com>
 *	    Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/thunderbolt.h>

#define DMA_TEST_TX_RING_SIZE		64
#define DMA_TEST_RX_RING_SIZE		256
#define DMA_TEST_FRAME_SIZE		SZ_4K
#define DMA_TEST_DATA_PATTERN		0x0123456789abcdefLL
#define DMA_TEST_MAX_PACKETS		1000

enum dma_test_frame_pdf {
	DMA_TEST_PDF_FRAME_START = 1,
	DMA_TEST_PDF_FRAME_END,
};

struct dma_test_frame {
	struct dma_test *dma_test;
	void *data;
	struct ring_frame frame;
};

enum dma_test_test_error {
	DMA_TEST_NO_ERROR,
	DMA_TEST_INTERRUPTED,
	DMA_TEST_BUFFER_ERROR,
	DMA_TEST_DMA_ERROR,
	DMA_TEST_CONFIG_ERROR,
	DMA_TEST_SPEED_ERROR,
	DMA_TEST_WIDTH_ERROR,
	DMA_TEST_BONDING_ERROR,
	DMA_TEST_PACKET_ERROR,
};

static const char * const dma_test_error_names[] = {
	[DMA_TEST_NO_ERROR] = "no errors",
	[DMA_TEST_INTERRUPTED] = "interrupted by signal",
	[DMA_TEST_BUFFER_ERROR] = "no memory for packet buffers",
	[DMA_TEST_DMA_ERROR] = "DMA ring setup failed",
	[DMA_TEST_CONFIG_ERROR] = "configuration is not valid",
	[DMA_TEST_SPEED_ERROR] = "unexpected link speed",
	[DMA_TEST_WIDTH_ERROR] = "unexpected link width",
	[DMA_TEST_BONDING_ERROR] = "lane bonding configuration error",
	[DMA_TEST_PACKET_ERROR] = "packet check failed",
};

enum dma_test_result {
	DMA_TEST_NOT_RUN,
	DMA_TEST_SUCCESS,
	DMA_TEST_FAIL,
};

static const char * const dma_test_result_names[] = {
	[DMA_TEST_NOT_RUN] = "not run",
	[DMA_TEST_SUCCESS] = "success",
	[DMA_TEST_FAIL] = "failed",
};

/**
 * struct dma_test - DMA test device driver private data
 * @svc: XDomain service the driver is bound to
 * @xd: XDomain the service belongs to
 * @rx_ring: Software ring holding RX frames
 * @rx_hopid: HopID used for receiving frames
 * @tx_ring: Software ring holding TX frames
 * @tx_hopid: HopID used for sending fames
 * @packets_to_send: Number of packets to send
 * @packets_to_receive: Number of packets to receive
 * @packets_sent: Actual number of packets sent
 * @packets_received: Actual number of packets received
 * @link_speed: Expected link speed (Gb/s), %0 to use whatever is negotiated
 * @link_width: Expected link width (Gb/s), %0 to use whatever is negotiated
 * @crc_errors: Number of CRC errors during the test run
 * @buffer_overflow_errors: Number of buffer overflow errors during the test
 *			    run
 * @result: Result of the last run
 * @error_code: Error code of the last run
 * @complete: Used to wait for the Rx to complete
 * @lock: Lock serializing access to this structure
 * @debugfs_dir: dentry of this dma_test
 */
struct dma_test {
	const struct tb_service *svc;
	struct tb_xdomain *xd;
	struct tb_ring *rx_ring;
	int rx_hopid;
	struct tb_ring *tx_ring;
	int tx_hopid;
	unsigned int packets_to_send;
	unsigned int packets_to_receive;
	unsigned int packets_sent;
	unsigned int packets_received;
	unsigned int link_speed;
	unsigned int link_width;
	unsigned int crc_errors;
	unsigned int buffer_overflow_errors;
	enum dma_test_result result;
	enum dma_test_test_error error_code;
	struct completion complete;
	struct mutex lock;
	struct dentry *debugfs_dir;
};

/* DMA test property directory UUID: 3188cd10-6523-4a5a-a682-fdca07a248d8 */
static const uuid_t dma_test_dir_uuid =
	UUID_INIT(0x3188cd10, 0x6523, 0x4a5a,
		  0xa6, 0x82, 0xfd, 0xca, 0x07, 0xa2, 0x48, 0xd8);

static struct tb_property_dir *dma_test_dir;
static void *dma_test_pattern;

static void dma_test_free_rings(struct dma_test *dt)
{
	if (dt->rx_ring) {
		tb_xdomain_release_in_hopid(dt->xd, dt->rx_hopid);
		tb_ring_free(dt->rx_ring);
		dt->rx_ring = NULL;
	}
	if (dt->tx_ring) {
		tb_xdomain_release_out_hopid(dt->xd, dt->tx_hopid);
		tb_ring_free(dt->tx_ring);
		dt->tx_ring = NULL;
	}
}

static int dma_test_start_rings(struct dma_test *dt)
{
	unsigned int flags = RING_FLAG_FRAME;
	struct tb_xdomain *xd = dt->xd;
	int ret, e2e_tx_hop = 0;
	struct tb_ring *ring;

	/*
	 * If we are both sender and receiver (traffic goes over a
	 * special loopback dongle) enable E2E flow control. This avoids
	 * losing packets.
	 */
	if (dt->packets_to_send && dt->packets_to_receive)
		flags |= RING_FLAG_E2E;

	if (dt->packets_to_send) {
		ring = tb_ring_alloc_tx(xd->tb->nhi, -1, DMA_TEST_TX_RING_SIZE,
					flags);
		if (!ring)
			return -ENOMEM;

		dt->tx_ring = ring;
		e2e_tx_hop = ring->hop;

		ret = tb_xdomain_alloc_out_hopid(xd, -1);
		if (ret < 0) {
			dma_test_free_rings(dt);
			return ret;
		}

		dt->tx_hopid = ret;
	}

	if (dt->packets_to_receive) {
		u16 sof_mask, eof_mask;

		sof_mask = BIT(DMA_TEST_PDF_FRAME_START);
		eof_mask = BIT(DMA_TEST_PDF_FRAME_END);

		ring = tb_ring_alloc_rx(xd->tb->nhi, -1, DMA_TEST_RX_RING_SIZE,
					flags, e2e_tx_hop, sof_mask, eof_mask,
					NULL, NULL);
		if (!ring) {
			dma_test_free_rings(dt);
			return -ENOMEM;
		}

		dt->rx_ring = ring;

		ret = tb_xdomain_alloc_in_hopid(xd, -1);
		if (ret < 0) {
			dma_test_free_rings(dt);
			return ret;
		}

		dt->rx_hopid = ret;
	}

	ret = tb_xdomain_enable_paths(dt->xd, dt->tx_hopid,
				      dt->tx_ring ? dt->tx_ring->hop : -1,
				      dt->rx_hopid,
				      dt->rx_ring ? dt->rx_ring->hop : -1);
	if (ret) {
		dma_test_free_rings(dt);
		return ret;
	}

	if (dt->tx_ring)
		tb_ring_start(dt->tx_ring);
	if (dt->rx_ring)
		tb_ring_start(dt->rx_ring);

	return 0;
}

static void dma_test_stop_rings(struct dma_test *dt)
{
	int ret;

	if (dt->rx_ring)
		tb_ring_stop(dt->rx_ring);
	if (dt->tx_ring)
		tb_ring_stop(dt->tx_ring);

	ret = tb_xdomain_disable_paths(dt->xd, dt->tx_hopid,
				       dt->tx_ring ? dt->tx_ring->hop : -1,
				       dt->rx_hopid,
				       dt->rx_ring ? dt->rx_ring->hop : -1);
	if (ret)
		dev_warn(&dt->svc->dev, "failed to disable DMA paths\n");

	dma_test_free_rings(dt);
}

static void dma_test_rx_callback(struct tb_ring *ring, struct ring_frame *frame,
				 bool canceled)
{
	struct dma_test_frame *tf = container_of(frame, typeof(*tf), frame);
	struct dma_test *dt = tf->dma_test;
	struct device *dma_dev = tb_ring_dma_device(dt->rx_ring);

	dma_unmap_single(dma_dev, tf->frame.buffer_phy, DMA_TEST_FRAME_SIZE,
			 DMA_FROM_DEVICE);
	kfree(tf->data);

	if (canceled) {
		kfree(tf);
		return;
	}

	dt->packets_received++;
	dev_dbg(&dt->svc->dev, "packet %u/%u received\n", dt->packets_received,
		dt->packets_to_receive);

	if (tf->frame.flags & RING_DESC_CRC_ERROR)
		dt->crc_errors++;
	if (tf->frame.flags & RING_DESC_BUFFER_OVERRUN)
		dt->buffer_overflow_errors++;

	kfree(tf);

	if (dt->packets_received == dt->packets_to_receive)
		complete(&dt->complete);
}

static int dma_test_submit_rx(struct dma_test *dt, size_t npackets)
{
	struct device *dma_dev = tb_ring_dma_device(dt->rx_ring);
	int i;

	for (i = 0; i < npackets; i++) {
		struct dma_test_frame *tf;
		dma_addr_t dma_addr;

		tf = kzalloc(sizeof(*tf), GFP_KERNEL);
		if (!tf)
			return -ENOMEM;

		tf->data = kzalloc(DMA_TEST_FRAME_SIZE, GFP_KERNEL);
		if (!tf->data) {
			kfree(tf);
			return -ENOMEM;
		}

		dma_addr = dma_map_single(dma_dev, tf->data, DMA_TEST_FRAME_SIZE,
					  DMA_FROM_DEVICE);
		if (dma_mapping_error(dma_dev, dma_addr)) {
			kfree(tf->data);
			kfree(tf);
			return -ENOMEM;
		}

		tf->frame.buffer_phy = dma_addr;
		tf->frame.callback = dma_test_rx_callback;
		tf->dma_test = dt;
		INIT_LIST_HEAD(&tf->frame.list);

		tb_ring_rx(dt->rx_ring, &tf->frame);
	}

	return 0;
}

static void dma_test_tx_callback(struct tb_ring *ring, struct ring_frame *frame,
				 bool canceled)
{
	struct dma_test_frame *tf = container_of(frame, typeof(*tf), frame);
	struct dma_test *dt = tf->dma_test;
	struct device *dma_dev = tb_ring_dma_device(dt->tx_ring);

	dma_unmap_single(dma_dev, tf->frame.buffer_phy, DMA_TEST_FRAME_SIZE,
			 DMA_TO_DEVICE);
	kfree(tf->data);
	kfree(tf);
}

static int dma_test_submit_tx(struct dma_test *dt, size_t npackets)
{
	struct device *dma_dev = tb_ring_dma_device(dt->tx_ring);
	int i;

	for (i = 0; i < npackets; i++) {
		struct dma_test_frame *tf;
		dma_addr_t dma_addr;

		tf = kzalloc(sizeof(*tf), GFP_KERNEL);
		if (!tf)
			return -ENOMEM;

		tf->frame.size = 0; /* means 4096 */
		tf->dma_test = dt;

		tf->data = kmemdup(dma_test_pattern, DMA_TEST_FRAME_SIZE, GFP_KERNEL);
		if (!tf->data) {
			kfree(tf);
			return -ENOMEM;
		}

		dma_addr = dma_map_single(dma_dev, tf->data, DMA_TEST_FRAME_SIZE,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(dma_dev, dma_addr)) {
			kfree(tf->data);
			kfree(tf);
			return -ENOMEM;
		}

		tf->frame.buffer_phy = dma_addr;
		tf->frame.callback = dma_test_tx_callback;
		tf->frame.sof = DMA_TEST_PDF_FRAME_START;
		tf->frame.eof = DMA_TEST_PDF_FRAME_END;
		INIT_LIST_HEAD(&tf->frame.list);

		dt->packets_sent++;
		dev_dbg(&dt->svc->dev, "packet %u/%u sent\n", dt->packets_sent,
			dt->packets_to_send);

		tb_ring_tx(dt->tx_ring, &tf->frame);
	}

	return 0;
}

#define DMA_TEST_DEBUGFS_ATTR(__fops, __get, __validate, __set)	\
static int __fops ## _show(void *data, u64 *val)		\
{								\
	struct tb_service *svc = data;				\
	struct dma_test *dt = tb_service_get_drvdata(svc);	\
	int ret;						\
								\
	ret = mutex_lock_interruptible(&dt->lock);		\
	if (ret)						\
		return ret;					\
	__get(dt, val);						\
	mutex_unlock(&dt->lock);				\
	return 0;						\
}								\
static int __fops ## _store(void *data, u64 val)		\
{								\
	struct tb_service *svc = data;				\
	struct dma_test *dt = tb_service_get_drvdata(svc);	\
	int ret;						\
								\
	ret = __validate(val);					\
	if (ret)						\
		return ret;					\
	ret = mutex_lock_interruptible(&dt->lock);		\
	if (ret)						\
		return ret;					\
	__set(dt, val);						\
	mutex_unlock(&dt->lock);				\
	return 0;						\
}								\
DEFINE_DEBUGFS_ATTRIBUTE(__fops ## _fops, __fops ## _show,	\
			 __fops ## _store, "%llu\n")

static void lanes_get(const struct dma_test *dt, u64 *val)
{
	*val = dt->link_width;
}

static int lanes_validate(u64 val)
{
	return val > 2 ? -EINVAL : 0;
}

static void lanes_set(struct dma_test *dt, u64 val)
{
	dt->link_width = val;
}
DMA_TEST_DEBUGFS_ATTR(lanes, lanes_get, lanes_validate, lanes_set);

static void speed_get(const struct dma_test *dt, u64 *val)
{
	*val = dt->link_speed;
}

static int speed_validate(u64 val)
{
	switch (val) {
	case 20:
	case 10:
	case 0:
		return 0;
	default:
		return -EINVAL;
	}
}

static void speed_set(struct dma_test *dt, u64 val)
{
	dt->link_speed = val;
}
DMA_TEST_DEBUGFS_ATTR(speed, speed_get, speed_validate, speed_set);

static void packets_to_receive_get(const struct dma_test *dt, u64 *val)
{
	*val = dt->packets_to_receive;
}

static int packets_to_receive_validate(u64 val)
{
	return val > DMA_TEST_MAX_PACKETS ? -EINVAL : 0;
}

static void packets_to_receive_set(struct dma_test *dt, u64 val)
{
	dt->packets_to_receive = val;
}
DMA_TEST_DEBUGFS_ATTR(packets_to_receive, packets_to_receive_get,
		      packets_to_receive_validate, packets_to_receive_set);

static void packets_to_send_get(const struct dma_test *dt, u64 *val)
{
	*val = dt->packets_to_send;
}

static int packets_to_send_validate(u64 val)
{
	return val > DMA_TEST_MAX_PACKETS ? -EINVAL : 0;
}

static void packets_to_send_set(struct dma_test *dt, u64 val)
{
	dt->packets_to_send = val;
}
DMA_TEST_DEBUGFS_ATTR(packets_to_send, packets_to_send_get,
		      packets_to_send_validate, packets_to_send_set);

static int dma_test_set_bonding(struct dma_test *dt)
{
	switch (dt->link_width) {
	case 2:
		return tb_xdomain_lane_bonding_enable(dt->xd);
	case 1:
		tb_xdomain_lane_bonding_disable(dt->xd);
		fallthrough;
	default:
		return 0;
	}
}

static bool dma_test_validate_config(struct dma_test *dt)
{
	if (!dt->packets_to_send && !dt->packets_to_receive)
		return false;
	if (dt->packets_to_send && dt->packets_to_receive &&
	    dt->packets_to_send != dt->packets_to_receive)
		return false;
	return true;
}

static void dma_test_check_errors(struct dma_test *dt, int ret)
{
	if (!dt->error_code) {
		if (dt->link_speed && dt->xd->link_speed != dt->link_speed) {
			dt->error_code = DMA_TEST_SPEED_ERROR;
		} else if (dt->link_width &&
			   dt->xd->link_width != dt->link_width) {
			dt->error_code = DMA_TEST_WIDTH_ERROR;
		} else if (dt->packets_to_send != dt->packets_sent ||
			 dt->packets_to_receive != dt->packets_received ||
			 dt->crc_errors || dt->buffer_overflow_errors) {
			dt->error_code = DMA_TEST_PACKET_ERROR;
		} else {
			return;
		}
	}

	dt->result = DMA_TEST_FAIL;
}

static int test_store(void *data, u64 val)
{
	struct tb_service *svc = data;
	struct dma_test *dt = tb_service_get_drvdata(svc);
	int ret;

	if (val != 1)
		return -EINVAL;

	ret = mutex_lock_interruptible(&dt->lock);
	if (ret)
		return ret;

	dt->packets_sent = 0;
	dt->packets_received = 0;
	dt->crc_errors = 0;
	dt->buffer_overflow_errors = 0;
	dt->result = DMA_TEST_SUCCESS;
	dt->error_code = DMA_TEST_NO_ERROR;

	dev_dbg(&svc->dev, "DMA test starting\n");
	if (dt->link_speed)
		dev_dbg(&svc->dev, "link_speed: %u Gb/s\n", dt->link_speed);
	if (dt->link_width)
		dev_dbg(&svc->dev, "link_width: %u\n", dt->link_width);
	dev_dbg(&svc->dev, "packets_to_send: %u\n", dt->packets_to_send);
	dev_dbg(&svc->dev, "packets_to_receive: %u\n", dt->packets_to_receive);

	if (!dma_test_validate_config(dt)) {
		dev_err(&svc->dev, "invalid test configuration\n");
		dt->error_code = DMA_TEST_CONFIG_ERROR;
		goto out_unlock;
	}

	ret = dma_test_set_bonding(dt);
	if (ret) {
		dev_err(&svc->dev, "failed to set lanes\n");
		dt->error_code = DMA_TEST_BONDING_ERROR;
		goto out_unlock;
	}

	ret = dma_test_start_rings(dt);
	if (ret) {
		dev_err(&svc->dev, "failed to enable DMA rings\n");
		dt->error_code = DMA_TEST_DMA_ERROR;
		goto out_unlock;
	}

	if (dt->packets_to_receive) {
		reinit_completion(&dt->complete);
		ret = dma_test_submit_rx(dt, dt->packets_to_receive);
		if (ret) {
			dev_err(&svc->dev, "failed to submit receive buffers\n");
			dt->error_code = DMA_TEST_BUFFER_ERROR;
			goto out_stop;
		}
	}

	if (dt->packets_to_send) {
		ret = dma_test_submit_tx(dt, dt->packets_to_send);
		if (ret) {
			dev_err(&svc->dev, "failed to submit transmit buffers\n");
			dt->error_code = DMA_TEST_BUFFER_ERROR;
			goto out_stop;
		}
	}

	if (dt->packets_to_receive) {
		ret = wait_for_completion_interruptible(&dt->complete);
		if (ret) {
			dt->error_code = DMA_TEST_INTERRUPTED;
			goto out_stop;
		}
	}

out_stop:
	dma_test_stop_rings(dt);
out_unlock:
	dma_test_check_errors(dt, ret);
	mutex_unlock(&dt->lock);

	dev_dbg(&svc->dev, "DMA test %s\n", dma_test_result_names[dt->result]);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(test_fops, NULL, test_store, "%llu\n");

static int status_show(struct seq_file *s, void *not_used)
{
	struct tb_service *svc = s->private;
	struct dma_test *dt = tb_service_get_drvdata(svc);
	int ret;

	ret = mutex_lock_interruptible(&dt->lock);
	if (ret)
		return ret;

	seq_printf(s, "result: %s\n", dma_test_result_names[dt->result]);
	if (dt->result == DMA_TEST_NOT_RUN)
		goto out_unlock;

	seq_printf(s, "packets received: %u\n", dt->packets_received);
	seq_printf(s, "packets sent: %u\n", dt->packets_sent);
	seq_printf(s, "CRC errors: %u\n", dt->crc_errors);
	seq_printf(s, "buffer overflow errors: %u\n",
		   dt->buffer_overflow_errors);
	seq_printf(s, "error: %s\n", dma_test_error_names[dt->error_code]);

out_unlock:
	mutex_unlock(&dt->lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(status);

static void dma_test_debugfs_init(struct tb_service *svc)
{
	struct dma_test *dt = tb_service_get_drvdata(svc);

	dt->debugfs_dir = debugfs_create_dir("dma_test", svc->debugfs_dir);

	debugfs_create_file("lanes", 0600, dt->debugfs_dir, svc, &lanes_fops);
	debugfs_create_file("speed", 0600, dt->debugfs_dir, svc, &speed_fops);
	debugfs_create_file("packets_to_receive", 0600, dt->debugfs_dir, svc,
			    &packets_to_receive_fops);
	debugfs_create_file("packets_to_send", 0600, dt->debugfs_dir, svc,
			    &packets_to_send_fops);
	debugfs_create_file("status", 0400, dt->debugfs_dir, svc, &status_fops);
	debugfs_create_file("test", 0200, dt->debugfs_dir, svc, &test_fops);
}

static int dma_test_probe(struct tb_service *svc, const struct tb_service_id *id)
{
	struct tb_xdomain *xd = tb_service_parent(svc);
	struct dma_test *dt;

	dt = devm_kzalloc(&svc->dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;

	dt->svc = svc;
	dt->xd = xd;
	mutex_init(&dt->lock);
	init_completion(&dt->complete);

	tb_service_set_drvdata(svc, dt);
	dma_test_debugfs_init(svc);

	return 0;
}

static void dma_test_remove(struct tb_service *svc)
{
	struct dma_test *dt = tb_service_get_drvdata(svc);

	mutex_lock(&dt->lock);
	debugfs_remove_recursive(dt->debugfs_dir);
	mutex_unlock(&dt->lock);
}

static int __maybe_unused dma_test_suspend(struct device *dev)
{
	/*
	 * No need to do anything special here. If userspace is writing
	 * to the test attribute when suspend started, it comes out from
	 * wait_for_completion_interruptible() with -ERESTARTSYS and the
	 * DMA test fails tearing down the rings. Once userspace is
	 * thawed the kernel restarts the write syscall effectively
	 * re-running the test.
	 */
	return 0;
}

static int __maybe_unused dma_test_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops dma_test_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dma_test_suspend, dma_test_resume)
};

static const struct tb_service_id dma_test_ids[] = {
	{ TB_SERVICE("dma_test", 1) },
	{ },
};
MODULE_DEVICE_TABLE(tbsvc, dma_test_ids);

static struct tb_service_driver dma_test_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "thunderbolt_dma_test",
		.pm = &dma_test_pm_ops,
	},
	.probe = dma_test_probe,
	.remove = dma_test_remove,
	.id_table = dma_test_ids,
};

static int __init dma_test_init(void)
{
	u64 data_value = DMA_TEST_DATA_PATTERN;
	int i, ret;

	dma_test_pattern = kmalloc(DMA_TEST_FRAME_SIZE, GFP_KERNEL);
	if (!dma_test_pattern)
		return -ENOMEM;

	for (i = 0; i <	DMA_TEST_FRAME_SIZE / sizeof(data_value); i++)
		((u32 *)dma_test_pattern)[i] = data_value++;

	dma_test_dir = tb_property_create_dir(&dma_test_dir_uuid);
	if (!dma_test_dir) {
		ret = -ENOMEM;
		goto err_free_pattern;
	}

	tb_property_add_immediate(dma_test_dir, "prtcid", 1);
	tb_property_add_immediate(dma_test_dir, "prtcvers", 1);
	tb_property_add_immediate(dma_test_dir, "prtcrevs", 0);
	tb_property_add_immediate(dma_test_dir, "prtcstns", 0);

	ret = tb_register_property_dir("dma_test", dma_test_dir);
	if (ret)
		goto err_free_dir;

	ret = tb_register_service_driver(&dma_test_driver);
	if (ret)
		goto err_unregister_dir;

	return 0;

err_unregister_dir:
	tb_unregister_property_dir("dma_test", dma_test_dir);
err_free_dir:
	tb_property_free_dir(dma_test_dir);
err_free_pattern:
	kfree(dma_test_pattern);

	return ret;
}
module_init(dma_test_init);

static void __exit dma_test_exit(void)
{
	tb_unregister_service_driver(&dma_test_driver);
	tb_unregister_property_dir("dma_test", dma_test_dir);
	tb_property_free_dir(dma_test_dir);
	kfree(dma_test_pattern);
}
module_exit(dma_test_exit);

MODULE_AUTHOR("Isaac Hazan <isaac.hazan@intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("DMA traffic test driver");
MODULE_LICENSE("GPL v2");
