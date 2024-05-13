// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Intel Corporation. */
#define dev_fmt(fmt) "Telemetry: " fmt

#include <asm/errno.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "adf_admin.h"
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_telemetry.h"

#define TL_IS_ZERO(input)	((input) == 0)

static bool is_tl_supported(struct adf_accel_dev *accel_dev)
{
	u16 fw_caps =  GET_HW_DATA(accel_dev)->fw_capabilities;

	return fw_caps & TL_CAPABILITY_BIT;
}

static int validate_tl_data(struct adf_tl_hw_data *tl_data)
{
	if (!tl_data->dev_counters ||
	    TL_IS_ZERO(tl_data->num_dev_counters) ||
	    !tl_data->sl_util_counters ||
	    !tl_data->sl_exec_counters ||
	    !tl_data->rp_counters ||
	    TL_IS_ZERO(tl_data->num_rp_counters))
		return -EOPNOTSUPP;

	return 0;
}

static int validate_tl_slice_counters(struct icp_qat_fw_init_admin_slice_cnt *slice_count,
				      u8 max_slices_per_type)
{
	u8 *sl_counter = (u8 *)slice_count;
	int i;

	for (i = 0; i < ADF_TL_SL_CNT_COUNT; i++) {
		if (sl_counter[i] > max_slices_per_type)
			return -EINVAL;
	}

	return 0;
}

static int adf_tl_alloc_mem(struct adf_accel_dev *accel_dev)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(accel_dev);
	struct device *dev = &GET_DEV(accel_dev);
	size_t regs_sz = tl_data->layout_sz;
	struct adf_telemetry *telemetry;
	int node = dev_to_node(dev);
	void *tl_data_regs;
	unsigned int i;

	telemetry = kzalloc_node(sizeof(*telemetry), GFP_KERNEL, node);
	if (!telemetry)
		return -ENOMEM;

	telemetry->rp_num_indexes = kmalloc_array(tl_data->max_rp,
						  sizeof(*telemetry->rp_num_indexes),
						  GFP_KERNEL);
	if (!telemetry->rp_num_indexes)
		goto err_free_tl;

	telemetry->regs_hist_buff = kmalloc_array(tl_data->num_hbuff,
						  sizeof(*telemetry->regs_hist_buff),
						  GFP_KERNEL);
	if (!telemetry->regs_hist_buff)
		goto err_free_rp_indexes;

	telemetry->regs_data = dma_alloc_coherent(dev, regs_sz,
						  &telemetry->regs_data_p,
						  GFP_KERNEL);
	if (!telemetry->regs_data)
		goto err_free_regs_hist_buff;

	for (i = 0; i < tl_data->num_hbuff; i++) {
		tl_data_regs = kzalloc_node(regs_sz, GFP_KERNEL, node);
		if (!tl_data_regs)
			goto err_free_dma;

		telemetry->regs_hist_buff[i] = tl_data_regs;
	}

	accel_dev->telemetry = telemetry;

	return 0;

err_free_dma:
	dma_free_coherent(dev, regs_sz, telemetry->regs_data,
			  telemetry->regs_data_p);

	while (i--)
		kfree(telemetry->regs_hist_buff[i]);

err_free_regs_hist_buff:
	kfree(telemetry->regs_hist_buff);
err_free_rp_indexes:
	kfree(telemetry->rp_num_indexes);
err_free_tl:
	kfree(telemetry);

	return -ENOMEM;
}

static void adf_tl_free_mem(struct adf_accel_dev *accel_dev)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(accel_dev);
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	struct device *dev = &GET_DEV(accel_dev);
	size_t regs_sz = tl_data->layout_sz;
	unsigned int i;

	for (i = 0; i < tl_data->num_hbuff; i++)
		kfree(telemetry->regs_hist_buff[i]);

	dma_free_coherent(dev, regs_sz, telemetry->regs_data,
			  telemetry->regs_data_p);

	kfree(telemetry->regs_hist_buff);
	kfree(telemetry->rp_num_indexes);
	kfree(telemetry);
	accel_dev->telemetry = NULL;
}

static unsigned long get_next_timeout(void)
{
	return msecs_to_jiffies(ADF_TL_TIMER_INT_MS);
}

static void snapshot_regs(struct adf_telemetry *telemetry, size_t size)
{
	void *dst = telemetry->regs_hist_buff[telemetry->hb_num];
	void *src = telemetry->regs_data;

	memcpy(dst, src, size);
}

static void tl_work_handler(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct adf_telemetry *telemetry;
	struct adf_tl_hw_data *tl_data;
	u32 msg_cnt, old_msg_cnt;
	size_t layout_sz;
	u32 *regs_data;
	size_t id;

	delayed_work = to_delayed_work(work);
	telemetry = container_of(delayed_work, struct adf_telemetry, work_ctx);
	tl_data = &GET_TL_DATA(telemetry->accel_dev);
	regs_data = telemetry->regs_data;

	id = tl_data->msg_cnt_off / sizeof(*regs_data);
	layout_sz = tl_data->layout_sz;

	if (!atomic_read(&telemetry->state)) {
		cancel_delayed_work_sync(&telemetry->work_ctx);
		return;
	}

	msg_cnt = regs_data[id];
	old_msg_cnt = msg_cnt;
	if (msg_cnt == telemetry->msg_cnt)
		goto out;

	mutex_lock(&telemetry->regs_hist_lock);

	snapshot_regs(telemetry, layout_sz);

	/* Check if data changed while updating it */
	msg_cnt = regs_data[id];
	if (old_msg_cnt != msg_cnt)
		snapshot_regs(telemetry, layout_sz);

	telemetry->msg_cnt = msg_cnt;
	telemetry->hb_num++;
	telemetry->hb_num %= telemetry->hbuffs;

	mutex_unlock(&telemetry->regs_hist_lock);

out:
	adf_misc_wq_queue_delayed_work(&telemetry->work_ctx, get_next_timeout());
}

int adf_tl_halt(struct adf_accel_dev *accel_dev)
{
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	struct device *dev = &GET_DEV(accel_dev);
	int ret;

	cancel_delayed_work_sync(&telemetry->work_ctx);
	atomic_set(&telemetry->state, 0);

	ret = adf_send_admin_tl_stop(accel_dev);
	if (ret)
		dev_err(dev, "failed to stop telemetry\n");

	return ret;
}

int adf_tl_run(struct adf_accel_dev *accel_dev, int state)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(accel_dev);
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	struct device *dev = &GET_DEV(accel_dev);
	size_t layout_sz = tl_data->layout_sz;
	int ret;

	ret = adf_send_admin_tl_start(accel_dev, telemetry->regs_data_p,
				      layout_sz, telemetry->rp_num_indexes,
				      &telemetry->slice_cnt);
	if (ret) {
		dev_err(dev, "failed to start telemetry\n");
		return ret;
	}

	ret = validate_tl_slice_counters(&telemetry->slice_cnt, tl_data->max_sl_cnt);
	if (ret) {
		dev_err(dev, "invalid value returned by FW\n");
		adf_send_admin_tl_stop(accel_dev);
		return ret;
	}

	telemetry->hbuffs = state;
	atomic_set(&telemetry->state, state);

	adf_misc_wq_queue_delayed_work(&telemetry->work_ctx, get_next_timeout());

	return 0;
}

int adf_tl_init(struct adf_accel_dev *accel_dev)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(accel_dev);
	u8 max_rp = GET_TL_DATA(accel_dev).max_rp;
	struct device *dev = &GET_DEV(accel_dev);
	struct adf_telemetry *telemetry;
	unsigned int i;
	int ret;

	ret = validate_tl_data(tl_data);
	if (ret)
		return ret;

	ret = adf_tl_alloc_mem(accel_dev);
	if (ret) {
		dev_err(dev, "failed to initialize: %d\n", ret);
		return ret;
	}

	telemetry = accel_dev->telemetry;
	telemetry->accel_dev = accel_dev;

	mutex_init(&telemetry->wr_lock);
	mutex_init(&telemetry->regs_hist_lock);
	INIT_DELAYED_WORK(&telemetry->work_ctx, tl_work_handler);

	for (i = 0; i < max_rp; i++)
		telemetry->rp_num_indexes[i] = ADF_TL_RP_REGS_DISABLED;

	return 0;
}

int adf_tl_start(struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);

	if (!accel_dev->telemetry)
		return -EOPNOTSUPP;

	if (!is_tl_supported(accel_dev)) {
		dev_info(dev, "feature not supported by FW\n");
		adf_tl_free_mem(accel_dev);
		return -EOPNOTSUPP;
	}

	return 0;
}

void adf_tl_stop(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->telemetry)
		return;

	if (atomic_read(&accel_dev->telemetry->state))
		adf_tl_halt(accel_dev);
}

void adf_tl_shutdown(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->telemetry)
		return;

	adf_tl_free_mem(accel_dev);
}
