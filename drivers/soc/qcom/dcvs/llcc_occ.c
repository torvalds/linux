// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/qcom_scm.h>
#include <linux/qtee_shmbridge.h>
#include <linux/slab.h>
#include "bus_prof.h"
#include "trace-bus-prof.h"

#define SAMPLE_MS	10
#define DDR_OUT_BUF_MAGIC	(0xDD4)

enum cmd {
	LLCC_OCC_START_PROFILING = 1,
	LLCC_OCC_GET_DATA = 2,
	LLCC_OCC_STOP_PROFILING = 3,
};

enum error {
	E_SUCCESS = 0, /* Operation successful */
	E_FAILURE = 1, /* Operation failed due to unknown err*/
	E_NULL_PARAM = 2, /* Null Parameter */
	E_INVALID_ARG = 3, /* Arg is not recognized */
	E_BAD_ADDRESS  = 4, /* Ptr arg is bad address */
	E_INVALID_ARG_LEN = 5, /* Arg length is wrong */
	E_NOT_SUPPORTED = 6, /* Operation not supported */
	E_UNINITIALIZED = 7, /* Operation not permitted on platform */
	E_PARTIAL_DUMP = 8, /* Operation not permitted right now */
	E_RESERVED = 0x7FFFFFFF
};

enum llcc_occ_masters {
	CPU = 0,
	GPU,
	NSP,
	MAX_MASTER,
};

static char *master_names[MAX_MASTER] = {"CPU", "GPU", "NSP"};

struct llcc_occ_start_req {
	u32		cmd_id;
	u32		active_masters;
} __packed;

struct llcc_occ_rsp {
	u32		cmd_id;
	enum error	status;
} __packed;

struct llcc_occ_get_req {
	u32		cmd_id;
	u8		*buf_ptr;
	u32		buf_size;
	u32		type; /* Stop : 0, Reset : 1 */
} __packed;

struct llcc_occ_stop_req {
	u32		cmd_id;
} __packed;

/* cmd_id is expected to be defined first for each member of the union */
union llcc_occ_req {
	struct llcc_occ_start_req start_req;
	struct llcc_occ_get_req get_req;
	struct llcc_occ_stop_req stop_req;
} __packed;

struct llcc_cmd_buf {
	union		llcc_occ_req llcc_occ_req;
	struct		llcc_occ_rsp llcc_occ_resp;
	u32		req_size;
} __packed;

struct llcc_occ_data {
	struct	llcc_occ_buf	master_buf[MAX_MASTER];
	enum	error		err;
	u16			magic;
	u64			qtime;
} __packed;

struct occ_sample {
	u64			ts;
	u32			curr_cap;
	u32			max_cap;
} __packed;

struct llcc_occ_dev_data {
	struct work_struct	work;
	struct workqueue_struct	*wq;
	struct hrtimer		hrtimer;
	u16			max_samples;
	u16			size_of_line;
	u32			active_masters;
	u32			available_masters;
	struct llcc_occ_data	*data;
	struct mutex		lock;
};

struct master_data {
	u16			curr_idx;
	u16			unread_samples;
	struct occ_sample	*occ_data;
	char			buf[PAGE_SIZE];
};

static struct master_data mdata[MAX_MASTER];
static struct dentry *llcc_occ_dir;
static struct llcc_occ_dev_data *llcc_occ;

static ssize_t get_last_samples(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	int index = 0, ret = 0, m_idx;
	int i = 0, size = 0, enable;
	char *master_name;

	mutex_lock(&llcc_occ->lock);
	if (!llcc_occ->active_masters) {
		pr_err("No master is enabled for LLCC occupancy\n");
		goto unlock;
	}

	master_name = file->private_data;
	for (m_idx = 0; m_idx < MAX_MASTER ; m_idx++) {
		if (!strcasecmp(master_names[m_idx], master_name))
			break;
	}

	enable = (llcc_occ->active_masters & BIT(m_idx));
	if (!enable) {
		pr_err("%s LLCC occupancy is not enabled\n", master_names[m_idx]);
		ret = -EINVAL;
		goto unlock;
	}

	index = (mdata[m_idx].curr_idx - mdata[m_idx].unread_samples +
				llcc_occ->max_samples) % llcc_occ->max_samples;
	for (i = 0; i < mdata[m_idx].unread_samples; i++) {
		size += scnprintf(mdata[m_idx].buf + size, PAGE_SIZE - size,
			"%llx\t%x\t%x\n", mdata[m_idx].occ_data[index].ts,
			mdata[m_idx].occ_data[index].curr_cap,
			mdata[m_idx].occ_data[index].max_cap);
			index = (index + 1) % llcc_occ->max_samples;
	}

	mdata[m_idx].unread_samples = 0;
	ret = simple_read_from_buffer(user_buf, count, ppos, mdata[m_idx].buf, size);
unlock:
	mutex_unlock(&llcc_occ->lock);

	return ret;
}

static int send_llcc_occ_profiling_command(const void *req)
{
	size_t req_size, rsp_size;
	struct llcc_occ_rsp *rsp;
	struct qtee_shm shm = {0};
	u32 qseos_cmd_id;
	int ret;

	if (!req)
		return -EINVAL;

	rsp = &((struct llcc_cmd_buf *)req)->llcc_occ_resp;
	rsp_size = sizeof(struct llcc_occ_rsp);
	req_size = ((struct llcc_cmd_buf *)req)->req_size;
	qseos_cmd_id = *(u32 *)req;
	ret = qtee_shmbridge_allocate_shm(PAGE_ALIGN(req_size + rsp_size), &shm);
	if (ret) {
		pr_err("qtee_shmbridge_allocate_shm failed, ret :%d\n", ret);
		return -ENOMEM;
	}

	memcpy(shm.vaddr, req, req_size);
	qtee_shmbridge_flush_shm_buf(&shm);
	switch (qseos_cmd_id) {
	case LLCC_OCC_START_PROFILING:
	case LLCC_OCC_GET_DATA:
	case LLCC_OCC_STOP_PROFILING:
		/* Send the command to TZ */
		ret = qcom_scm_get_llcc_occupancy(shm.paddr, req_size,
						shm.paddr + req_size, rsp_size);
		break;
	default:
		pr_err("cmd_id %d is not supported.\n", qseos_cmd_id);
		ret = -EINVAL;
	}

	qtee_shmbridge_inv_shm_buf(&shm);
	memcpy(rsp, (char *)shm.vaddr + req_size, rsp_size);
	qtee_shmbridge_free_shm(&shm);
	/* Verify cmd id and Check that request succeeded. */
	if (rsp->status != 0 ||
		qseos_cmd_id != rsp->cmd_id) {
		ret = -1;
		pr_err("Status: %d,Cmd: %d\n",
			rsp->status,
			rsp->cmd_id);
	}

	return ret;
}

static int start_memory_occ_stats(void)
{
	struct llcc_cmd_buf *llcc_cmd_buf;
	int ret;

	llcc_cmd_buf = kzalloc(sizeof(*llcc_cmd_buf), GFP_KERNEL);
	if (!llcc_cmd_buf)
		return -ENOMEM;

	llcc_cmd_buf->llcc_occ_req.start_req.cmd_id = LLCC_OCC_START_PROFILING;
	llcc_cmd_buf->llcc_occ_req.start_req.active_masters = llcc_occ->active_masters;
	llcc_cmd_buf->req_size = sizeof(struct llcc_occ_start_req);
	ret = send_llcc_occ_profiling_command(llcc_cmd_buf);
	if (ret) {
		pr_err("Error in %s, ret = %d\n", __func__, ret);
		goto out;
	}
	if (!hrtimer_active(&llcc_occ->hrtimer))
		hrtimer_start(&llcc_occ->hrtimer,
				ms_to_ktime(SAMPLE_MS), HRTIMER_MODE_REL_PINNED);
out:
	kfree(llcc_cmd_buf);

	return ret;
}

static int stop_memory_occ_stats(void)
{
	struct llcc_cmd_buf *llcc_cmd_buf;
	int ret;

	hrtimer_cancel(&llcc_occ->hrtimer);
	cancel_work_sync(&llcc_occ->work);
	llcc_cmd_buf = kzalloc(sizeof(*llcc_cmd_buf), GFP_KERNEL);

	if (!llcc_cmd_buf)
		return -ENOMEM;

	llcc_cmd_buf->llcc_occ_req.stop_req.cmd_id = LLCC_OCC_STOP_PROFILING;
	llcc_cmd_buf->req_size = sizeof(struct llcc_occ_stop_req);
	ret = send_llcc_occ_profiling_command(llcc_cmd_buf);
	if (ret)
		pr_err("Error in %s, ret = %d\n", __func__, ret);
	kfree(llcc_cmd_buf);

	return 0;
}

static int set_mon_enabled(void *data, u64 val)
{
	char *master_name = data;
	u32 count, enable = val ? 1 : 0;
	int i, ret = 0;

	mutex_lock(&llcc_occ->lock);
	for (i = 0; i < MAX_MASTER; i++) {
		if (!strcasecmp(master_names[i], master_name))
			break;
	}
	if (enable == (llcc_occ->active_masters & BIT(i)))
		goto unlock;
	count = hweight32(llcc_occ->active_masters);
	if (count >= MAX_MASTER && enable) {
		pr_err("Max masters already enabled\n");
		ret = -EINVAL;
		goto unlock;
	}
	mutex_unlock(&llcc_occ->lock);
	if (count)
		stop_memory_occ_stats();
	mutex_lock(&llcc_occ->lock);
	llcc_occ->active_masters = (llcc_occ->active_masters ^ BIT(i));
	if (llcc_occ->active_masters)
		start_memory_occ_stats();
unlock:
	mutex_unlock(&llcc_occ->lock);

	return ret;
}

static int get_mon_enabled(void *data, u64 *val)
{
	char *master_name = data;
	int i;

	mutex_lock(&llcc_occ->lock);

	for (i = 0; i < MAX_MASTER; i++) {
		if (!strcasecmp(master_names[i], master_name))
			break;
	}
	if (llcc_occ->active_masters & BIT(i))
		*val = 1;
	else
		*val = 0;
	mutex_unlock(&llcc_occ->lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(set_mon_enabled_ops, get_mon_enabled, set_mon_enabled, "%llu\n");

static const struct file_operations show_last_samples_ops = {
	.read = get_last_samples,
	.open = simple_open,
	.llseek = default_llseek,
};

static void llcc_occ_update_work(struct work_struct *work)
{
	const int bufsize = sizeof(struct llcc_occ_data);
	struct llcc_cmd_buf *llcc_cmd_buf;
	struct qtee_shm buf_shm = {0};
	int ret = 0, i, m;
	u16 magic, enable;

	llcc_cmd_buf = kzalloc(sizeof(*llcc_cmd_buf), GFP_KERNEL);
	if (!llcc_cmd_buf)
		return;
	ret = qtee_shmbridge_allocate_shm(PAGE_ALIGN(bufsize), &buf_shm);
	if (ret) {
		pr_err("shmbridge alloc buf failed\n");
		return;
	}

	llcc_cmd_buf->llcc_occ_req.get_req.cmd_id = LLCC_OCC_GET_DATA;
	llcc_cmd_buf->llcc_occ_req.get_req.buf_ptr = (u8 *)buf_shm.paddr;
	llcc_cmd_buf->llcc_occ_req.get_req.buf_size = bufsize;
	llcc_cmd_buf->llcc_occ_req.get_req.type = 1;
	llcc_cmd_buf->req_size = sizeof(struct llcc_occ_get_req);
	qtee_shmbridge_flush_shm_buf(&buf_shm);
	ret = send_llcc_occ_profiling_command(llcc_cmd_buf);

	if (ret) {
		pr_err("send_llcc_occ_profiling_command failed\n");
		goto err;
	}

	qtee_shmbridge_inv_shm_buf(&buf_shm);
	memcpy(llcc_occ->data, (char *)buf_shm.vaddr, sizeof(*llcc_occ->data));
	magic = llcc_occ->data->magic;
	if (magic != DDR_OUT_BUF_MAGIC) {
		pr_err("Expected magic value is %x but got %x\n", DDR_OUT_BUF_MAGIC, magic);
		goto err;
	}

	mutex_lock(&llcc_occ->lock);
	for (i = 0; i < MAX_MASTER; i++) {
		m = llcc_occ->data->master_buf[i].master_id;
		if (m >= MAX_MASTER)
			continue;
		mdata[m].occ_data[mdata[m].curr_idx].ts = llcc_occ->data->qtime;
		mdata[m].occ_data[mdata[m].curr_idx].max_cap =
						llcc_occ->data->master_buf[i].max_cap;
		mdata[m].occ_data[mdata[m].curr_idx].curr_cap =
						llcc_occ->data->master_buf[i].curr_cap;
		mdata[m].unread_samples = min(++mdata[m].unread_samples, llcc_occ->max_samples);
		mdata[m].curr_idx = (mdata[m].curr_idx + 1) % llcc_occ->max_samples;
	}

	for (i = 0; i < MAX_MASTER ; i++) {
		enable = (llcc_occ->active_masters & BIT(i));
		if (enable)
			trace_llcc_occupancy_last_sample(llcc_occ->data->qtime, i,
							llcc_occ->data->master_buf[i].curr_cap,
							llcc_occ->data->master_buf[i].max_cap);
	}
	mutex_unlock(&llcc_occ->lock);
err:
	qtee_shmbridge_free_shm(&buf_shm);
	kfree(llcc_cmd_buf);
}

static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer)
{
	ktime_t now = ktime_get();

	queue_work(llcc_occ->wq, &llcc_occ->work);
	hrtimer_forward(timer, now, ms_to_ktime(SAMPLE_MS));

	return HRTIMER_RESTART;
}

static int llcc_occ_create_fs_entries(void)
{
	int i;
	struct dentry  *ret, *master_dir;

	llcc_occ_dir = debugfs_create_dir("llcc_occ", 0);

	if (IS_ERR(llcc_occ_dir)) {
		pr_err("Debugfs directory creation failed for llcc_occ\n");
		return PTR_ERR(llcc_occ_dir);
	}

	for (i = 0; i < MAX_MASTER; i++) {
		master_dir = debugfs_create_dir(master_names[i], llcc_occ_dir);
		if (IS_ERR(master_dir)) {
			pr_err("Debugfs directory creation failed for %s\n", master_names[i]);
			goto error_cleanup;
		}

		ret = debugfs_create_file("show_last_samples", 0400, master_dir,
						master_names[i], &show_last_samples_ops);
		if (IS_ERR(ret)) {
			pr_err("Debugfs file creation failed for show_last_samples\n");
			goto error_cleanup;
		}

		ret = debugfs_create_file("enable", 0644, master_dir,
						master_names[i], &set_mon_enabled_ops);
		if (IS_ERR(ret)) {
			pr_err("Debugfs file creation failed for enable\n");
			goto error_cleanup;
		}
	}

	return 0;

error_cleanup:
	for (; i >= 0; i--)
		debugfs_remove_recursive(debugfs_lookup(master_names[i], llcc_occ_dir));
	debugfs_remove_recursive(llcc_occ_dir);

	return -ENOENT;
}

static int __init qcom_llcc_occ_init(void)
{
	int ret, i, j;

	llcc_occ =  kzalloc(sizeof(*llcc_occ), GFP_KERNEL);
	if (!llcc_occ)
		return -ENOMEM;

	llcc_occ->data = kzalloc(sizeof(*llcc_occ->data), GFP_KERNEL);
	if (!llcc_occ->data) {
		kfree(llcc_occ);
		return -ENOMEM;
	}

	for (i = 0; i < MAX_MASTER; i++)
		llcc_occ->available_masters |= BIT(i);
	ret =  llcc_occ_create_fs_entries();
	if (ret < 0)
		goto err;
	/*
	 * to get no of hex char in a line multiplying size of struct occ_sample by 2
	 * and adding 1 for tab and 1 for newline.
	 * output format is qtime followed by occ data in hexa,example -> 32c7bd2 a
	 */
	llcc_occ->size_of_line = sizeof(struct occ_sample) * 2 + 2;
	llcc_occ->max_samples = PAGE_SIZE / llcc_occ->size_of_line;
	for (i = 0; i < MAX_MASTER; i++) {
		mdata[i].occ_data = kcalloc(llcc_occ->max_samples,
				sizeof(struct occ_sample), GFP_KERNEL);
		if (!mdata[i].occ_data) {
			ret = -ENOMEM;
			goto debugfs_file_err;
		}
	}

	mutex_init(&llcc_occ->lock);
	hrtimer_init(&llcc_occ->hrtimer, CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
	llcc_occ->hrtimer.function = hrtimer_handler;
	llcc_occ->wq = create_freezable_workqueue("llcc_occ_wq");
	if (!llcc_occ->wq) {
		pr_err("Couldn't create llcc_occ workqueue.\n");
		ret = -ENOMEM;
		goto debugfs_file_err;
	}
	INIT_WORK(&llcc_occ->work, &llcc_occ_update_work);

	return ret;

debugfs_file_err:
	for (j = 0; j < i; j++)
		kfree(mdata[j].occ_data);
	debugfs_remove_recursive(llcc_occ_dir);
err:
	kfree(llcc_occ->data);
	kfree(llcc_occ);

	return ret;
}

module_init(qcom_llcc_occ_init);

MODULE_DESCRIPTION("QCOM LLCC Occupancy driver");
MODULE_LICENSE("GPL");
