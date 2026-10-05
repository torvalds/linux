// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define MHI_LOOPBACK_DEFAULT_TRE_SIZE	32
#define MHI_LOOPBACK_DEFAULT_NUM_TRE	1
#define MHI_LOOPBACK_TIMEOUT_MS		5000
#define MHI_LOOPBACK_MAX_TRE_SIZE	(SZ_64K - 1)

struct mhi_loopback {
	struct mhi_device *mdev;
	/* Serializes the sysfs attributes against a running test */
	struct mutex lb_mutex;
	struct completion comp;
	/* Counts pending DL completions; comp fires when it hits zero */
	atomic_t tre_pending;
	u32 num_tre;
	u32 tre_size;
};

static ssize_t tre_size_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct mhi_loopback *loopback = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", loopback->tre_size);
}

static ssize_t tre_size_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct mhi_loopback *loopback = dev_get_drvdata(dev);
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if (val == 0 || val > MHI_LOOPBACK_MAX_TRE_SIZE)
		return -EINVAL;

	guard(mutex)(&loopback->lb_mutex);
	loopback->tre_size = val;

	return count;
}
static DEVICE_ATTR_RW(tre_size);

static ssize_t max_tre_size_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", MHI_LOOPBACK_MAX_TRE_SIZE);
}
static DEVICE_ATTR_RO(max_tre_size);

static int mhi_loopback_ring_capacity(struct mhi_loopback *loopback)
{
	return min(mhi_get_free_desc_count(loopback->mdev, DMA_TO_DEVICE),
		   mhi_get_free_desc_count(loopback->mdev, DMA_FROM_DEVICE));
}

static ssize_t max_num_tre_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mhi_loopback *loopback = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", mhi_loopback_ring_capacity(loopback));
}
static DEVICE_ATTR_RO(max_num_tre);

static ssize_t num_tre_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mhi_loopback *loopback = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", loopback->num_tre);
}

static ssize_t num_tre_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct mhi_loopback *loopback = dev_get_drvdata(dev);
	u32 val;
	int el_num;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if (val == 0)
		return -EINVAL;

	guard(mutex)(&loopback->lb_mutex);

	el_num = mhi_loopback_ring_capacity(loopback);
	if (val > el_num) {
		dev_err(dev, "num_tre (%u) exceeds ring capacity (%d)\n", val, el_num);
		return -EINVAL;
	}

	loopback->num_tre = val;

	return count;
}
static DEVICE_ATTR_RW(num_tre);

static void mhi_loopback_reset_chan(struct mhi_loopback *loopback)
{
	struct device *dev = &loopback->mdev->dev;
	int ret;

	mhi_unprepare_from_transfer(loopback->mdev);
	ret = mhi_prepare_for_transfer(loopback->mdev);
	if (ret)
		dev_err(dev, "Failed to re-prepare channel for transfers: %d\n", ret);
}

static ssize_t start_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct mhi_loopback *loopback = dev_get_drvdata(dev);
	u32 total_size, tre_count, tre_size;
	int i, ret;

	guard(mutex)(&loopback->lb_mutex);

	tre_size = loopback->tre_size;
	tre_count = loopback->num_tre;
	total_size = size_mul(tre_count, tre_size);

	if (total_size > KMALLOC_MAX_SIZE)
		return -EINVAL;

	if (tre_count > mhi_loopback_ring_capacity(loopback)) {
		dev_err(dev, "Not enough ring space for %u TREs\n", tre_count);
		return -ENOSPC;
	}

	void *recv_buf __free(kfree) = kzalloc(total_size, GFP_KERNEL);
	if (!recv_buf)
		return -ENOMEM;

	void *send_buf __free(kfree) = kzalloc(total_size, GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	get_random_bytes(send_buf, total_size);

	atomic_set(&loopback->tre_pending, tre_count);
	reinit_completion(&loopback->comp);

	for (i = 0; i < tre_count; i++) {
		ret = mhi_queue_buf(loopback->mdev, DMA_FROM_DEVICE,
				    recv_buf + (i * tre_size), tre_size, MHI_EOT);
		if (ret) {
			dev_err(dev, "Unable to queue read TRE %d: %d\n", i, ret);
			/*
			 * TREs already queued for this iteration still point at
			 * recv_buf, which is about to be freed. Reset the channel
			 * to reclaim them before returning.
			 */
			mhi_loopback_reset_chan(loopback);
			return ret;
		}
	}

	for (i = 0; i < tre_count - 1; i++) {
		ret = mhi_queue_buf(loopback->mdev, DMA_TO_DEVICE,
				    send_buf + (i * tre_size), tre_size, MHI_CHAIN);
		if (ret) {
			dev_err(dev, "Unable to queue send TRE %d: %d\n", i, ret);
			/* Reclaim the queued DL and UL TREs before recv_buf/send_buf are freed */
			mhi_loopback_reset_chan(loopback);
			return ret;
		}
	}

	ret = mhi_queue_buf(loopback->mdev, DMA_TO_DEVICE,
			    send_buf + (i * tre_size), tre_size, MHI_EOT);
	if (ret) {
		dev_err(dev, "Unable to queue final TRE: %d\n", ret);
		/* Reclaim the queued DL and UL TREs before recv_buf/send_buf are freed */
		mhi_loopback_reset_chan(loopback);
		return ret;
	}

	if (!wait_for_completion_timeout(&loopback->comp,
					 msecs_to_jiffies(MHI_LOOPBACK_TIMEOUT_MS))) {
		dev_err(dev, "Loopback test timed out\n");
		mhi_loopback_reset_chan(loopback);
		return -ETIMEDOUT;
	}

	if (memcmp(send_buf, recv_buf, total_size)) {
		dev_err(dev, "Loopback data mismatch\n");
		return -EIO;
	}

	return count;
}
static DEVICE_ATTR_WO(start);

static void mhi_loopback_dl_callback(struct mhi_device *mhi_dev,
				     struct mhi_result *mhi_res)
{
	struct mhi_loopback *loopback = dev_get_drvdata(&mhi_dev->dev);

	if (mhi_res->transaction_status && mhi_res->transaction_status != -ENOTCONN)
		dev_err(&mhi_dev->dev, "DL callback error: status %d\n",
			mhi_res->transaction_status);

	if (atomic_dec_and_test(&loopback->tre_pending))
		complete(&loopback->comp);
}

static void mhi_loopback_ul_callback(struct mhi_device *mhi_dev,
				     struct mhi_result *mhi_res)
{
	if (mhi_res->transaction_status && mhi_res->transaction_status != -ENOTCONN)
		dev_err(&mhi_dev->dev, "UL callback error: status %d\n",
			mhi_res->transaction_status);
}

static struct attribute *mhi_loopback_attrs[] = {
	&dev_attr_tre_size.attr,
	&dev_attr_max_tre_size.attr,
	&dev_attr_num_tre.attr,
	&dev_attr_max_num_tre.attr,
	&dev_attr_start.attr,
	NULL
};

static const struct attribute_group mhi_loopback_group = {
	.attrs = mhi_loopback_attrs,
};

static const struct attribute_group *mhi_loopback_groups[] = {
	&mhi_loopback_group,
	NULL,
};

static int mhi_loopback_probe(struct mhi_device *mhi_dev,
			      const struct mhi_device_id *id)
{
	struct mhi_loopback *loopback;
	int ret;

	loopback = devm_kzalloc(&mhi_dev->dev, sizeof(*loopback), GFP_KERNEL);
	if (!loopback)
		return -ENOMEM;

	loopback->mdev = mhi_dev;
	loopback->tre_size = MHI_LOOPBACK_DEFAULT_TRE_SIZE;
	loopback->num_tre = MHI_LOOPBACK_DEFAULT_NUM_TRE;

	mutex_init(&loopback->lb_mutex);
	init_completion(&loopback->comp);

	dev_set_drvdata(&mhi_dev->dev, loopback);

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		return dev_err_probe(&mhi_dev->dev, ret, "Failed to prepare for transfers\n");

	return 0;
}

static void mhi_loopback_remove(struct mhi_device *mhi_dev)
{
	mhi_unprepare_from_transfer(mhi_dev);
}

static const struct mhi_device_id mhi_loopback_id_table[] = {
	{ .chan = "LOOPBACK" },
	{ }
};
MODULE_DEVICE_TABLE(mhi, mhi_loopback_id_table);

static struct mhi_driver mhi_loopback_driver = {
	.probe = mhi_loopback_probe,
	.remove = mhi_loopback_remove,
	.dl_xfer_cb = mhi_loopback_dl_callback,
	.ul_xfer_cb = mhi_loopback_ul_callback,
	.id_table = mhi_loopback_id_table,
	.driver = {
		.name = "mhi_loopback",
		.dev_groups = mhi_loopback_groups,
	},
};

module_mhi_driver(mhi_loopback_driver);

MODULE_AUTHOR("Krishna Chaitanya Chundru <krishna.chundru@oss.qualcomm.com>");
MODULE_AUTHOR("Sumit Kumar <sumit.kumar@oss.qualcomm.com>");
MODULE_DESCRIPTION("MHI Host Loopback Driver");
MODULE_LICENSE("GPL");
