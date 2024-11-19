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
#define DDR_OUT_BUF_MAGIC (0xDD4)

enum cmd {
	LLCC_MISS_START_PROFILING = 1,
	LLCC_MISS_GET_DATA = 2,
	LLCC_MISS_STOP_PROFILING = 3,
};

enum error {
	E_SUCCESS = 0, /* Operation successful */
	E_FAILURE = 1, /* Operation failed due to unknown err*/
	E_NULL_PARAM = 2,/* Null Parameter */
	E_INVALID_ARG = 3,/* Arg is not recognized */
	E_BAD_ADDRESS  = 4,/* Ptr arg is bad address */
	E_INVALID_ARG_LEN = 5,/* Arg length is wrong */
	E_NOT_SUPPORTED = 6,/* Operation not supported */
	E_UNINITIALIZED = 7,/* Operation not permitted on platform */
	E_PARTIAL_DUMP = 8,/* Operation not permitted right now */
	E_RESERVED = 0x7FFFFFFF
};

enum llcc_miss_masters {
	CPU = 0,
	GPU,
	NSP,
	MAX_MASTER,
};

static char *master_names[MAX_MASTER] = {"CPU", "GPU", "NSP"};

struct llcc_miss_start_req {
	u32	cmd_id;
	u32	active_masters;
} __packed;

struct llcc_miss_resp {
	u32		cmd_id;
	enum error	status;
} __packed;

struct llcc_miss_get_req {
	u32		cmd_id;
	u8		*buf_ptr;
	u32		buf_size;
	u32		type; /* Stop : 0, Reset : 1 */
} __packed;

struct llcc_miss_stop_req {
	u32	cmd_id;
} __packed;

/* cmd_id is expected to be defined first for each member of the union */
union llcc_miss_req {
	struct llcc_miss_start_req start_req;
	struct llcc_miss_get_req get_req;
	struct llcc_miss_stop_req stop_req;
} __packed;

struct llcc_cmd_buf {
	union		llcc_miss_req llcc_miss_req;
	struct		llcc_miss_resp llcc_miss_resp;
	u32		req_size;
} __packed;

struct llcc_miss_data {
	struct	llcc_miss_buf	master_buf[MAX_CONCURRENT_MASTERS];
	enum	error		err;
	u16			magic;
	u64			qtime;
} __packed;

struct miss_sample {
	u64	ts;
	u16	measured_miss_rate;
} __packed;

struct llcc_miss_dev_data {
	struct work_struct	work;
	struct workqueue_struct	*wq;
	struct hrtimer		hrtimer;
	u16			max_samples;
	u16			size_of_line;
	u32			active_masters;
	u32			available_masters;
	struct llcc_miss_data	*data;
	struct mutex		lock;
};

struct master_data {
	u16			curr_idx;
	u16			unread_samples;
	struct miss_sample	*miss_data;
	char			buf[PAGE_SIZE];
};

static struct master_data mdata[MAX_MASTER];
static struct dentry *llcc_miss_dir;
static struct llcc_miss_dev_data *llcc_miss;

static ssize_t get_last_samples(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	int index = 0, ret = 0, m_idx;
	int i = 0, size = 0, enable;
	char *master_name;

	mutex_lock(&llcc_miss->lock);
	if (!llcc_miss->active_masters) {
		pr_err("No master is enabled for mem miss\n");
		goto unlock;
	}
	master_name = file->private_data;
	for (m_idx = 0; m_idx < MAX_MASTER ; m_idx++) {
		if (!strcasecmp(master_names[m_idx], master_name))
			break;
	}

	enable = (llcc_miss->active_masters & BIT(m_idx));
	if (!enable) {
		pr_err("%s memory miss is not enabled\n", master_names[m_idx]);
		ret = -EINVAL;
		goto unlock;
	}

	index = (mdata[m_idx].curr_idx - mdata[m_idx].unread_samples +
				llcc_miss->max_samples) % llcc_miss->max_samples;
	for (i = 0; i < mdata[m_idx].unread_samples; i++) {
		size += scnprintf(mdata[m_idx].buf + size, PAGE_SIZE - size,
			"%llx\t%x\n", mdata[m_idx].miss_data[index].ts,
			mdata[m_idx].miss_data[index].measured_miss_rate);
			index = (index + 1) % llcc_miss->max_samples;
	}

	mdata[m_idx].unread_samples = 0;
	ret = simple_read_from_buffer(user_buf, count, ppos, mdata[m_idx].buf, size);
unlock:
	mutex_unlock(&llcc_miss->lock);

	return ret;
}

static int send_llcc_miss_profiling_command(const void *req)
{
	int ret = 0;
	u32 qseos_cmd_id = 0;
	struct llcc_miss_resp *rsp = NULL;
	size_t req_size = 0, rsp_size = 0;
	struct qtee_shm shm = {0};

	if (!req)
		return -EINVAL;
	rsp = &((struct llcc_cmd_buf *)req)->llcc_miss_resp;
	rsp_size = sizeof(struct llcc_miss_resp);
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
	case LLCC_MISS_START_PROFILING:
	case LLCC_MISS_GET_DATA:
	case LLCC_MISS_STOP_PROFILING:
		/* Send the command to TZ */
		ret = qcom_scm_get_llcc_missrate(shm.paddr, req_size,
						shm.paddr + req_size, rsp_size);
		break;
	default:
		pr_err("cmd_id %d is not supported.\n",
			   qseos_cmd_id);
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

static int start_memory_miss_stats(void)
{
	int ret = 0;
	struct llcc_cmd_buf *llcc_cmd_buf = NULL;

	llcc_cmd_buf = kzalloc(sizeof(*llcc_cmd_buf), GFP_KERNEL);
	if (!llcc_cmd_buf)
		return -ENOMEM;
	llcc_cmd_buf->llcc_miss_req.start_req.cmd_id = LLCC_MISS_START_PROFILING;
	llcc_cmd_buf->llcc_miss_req.start_req.active_masters = llcc_miss->active_masters;
	llcc_cmd_buf->req_size = sizeof(struct llcc_miss_start_req);
	ret = send_llcc_miss_profiling_command(llcc_cmd_buf);
	if (ret) {
		pr_err("Error in %s, ret = %d\n", __func__, ret);
		goto out;
	}

	if (!hrtimer_active(&llcc_miss->hrtimer))
		hrtimer_start(&llcc_miss->hrtimer,
				ms_to_ktime(SAMPLE_MS), HRTIMER_MODE_REL_PINNED);
out:
	kfree(llcc_cmd_buf);

	return ret;
}

static int stop_memory_miss_stats(void)
{
	int ret;
	struct llcc_cmd_buf *llcc_cmd_buf = NULL;

	hrtimer_cancel(&llcc_miss->hrtimer);
	cancel_work_sync(&llcc_miss->work);
	llcc_cmd_buf = kzalloc(sizeof(*llcc_cmd_buf), GFP_KERNEL);
	if (!llcc_cmd_buf)
		return -ENOMEM;

	llcc_cmd_buf->llcc_miss_req.stop_req.cmd_id = LLCC_MISS_STOP_PROFILING;
	llcc_cmd_buf->req_size = sizeof(struct llcc_miss_stop_req);
	ret = send_llcc_miss_profiling_command(llcc_cmd_buf);
	if (ret)
		pr_err("Error in %s, ret = %d\n", __func__, ret);

	kfree(llcc_cmd_buf);

	return 0;
}

static int set_mon_enabled(void *data, u64 val)
{
	char *master_name = data;
	int i, ret = 0;
	u32 count, enable = val ? 1 : 0;

	mutex_lock(&llcc_miss->lock);
	for (i = 0; i < MAX_MASTER; i++) {
		if (!strcasecmp(master_names[i], master_name))
			break;
	}
	if (enable == (llcc_miss->active_masters & BIT(i)))
		goto unlock;
	count = hweight32(llcc_miss->active_masters);
	if (count >= MAX_CONCURRENT_MASTERS && enable) {
		pr_err("Max masters already enabled\n");
		ret = -EINVAL;
		goto unlock;
	}
	mutex_unlock(&llcc_miss->lock);
	if (count)
		stop_memory_miss_stats();
	mutex_lock(&llcc_miss->lock);
	llcc_miss->active_masters = (llcc_miss->active_masters ^ BIT(i));
	if (llcc_miss->active_masters)
		start_memory_miss_stats();

unlock:
	mutex_unlock(&llcc_miss->lock);

	return ret;
}

static int get_mon_enabled(void *data, u64 *val)
{
	char *master_name = data;
	int i;

	mutex_lock(&llcc_miss->lock);
	for (i = 0; i < MAX_MASTER; i++) {
		if (!strcasecmp(master_names[i], master_name))
			break;
	}

	if (llcc_miss->active_masters & BIT(i))
		*val = 1;
	else
		*val = 0;
	mutex_unlock(&llcc_miss->lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(set_mon_enabled_ops, get_mon_enabled, set_mon_enabled, "%llu\n");

static const struct file_operations show_last_samples_ops = {
	.read = get_last_samples,
	.open = simple_open,
	.llseek = default_llseek,
};

static void llcc_miss_update_work(struct work_struct *work)
{
	int ret = 0, i, m;
	u16 magic;
	struct llcc_cmd_buf *llcc_cmd_buf;
	struct qtee_shm buf_shm = {0};
	const int bufsize = sizeof(struct llcc_miss_data);

	llcc_cmd_buf = kzalloc(sizeof(*llcc_cmd_buf), GFP_KERNEL);
	if (!llcc_cmd_buf)
		return;
	ret = qtee_shmbridge_allocate_shm(PAGE_ALIGN(bufsize), &buf_shm);
	if (ret) {
		pr_err("shmbridge alloc buf failed\n");
		return;
	}

	llcc_cmd_buf->llcc_miss_req.get_req.cmd_id = LLCC_MISS_GET_DATA;
	llcc_cmd_buf->llcc_miss_req.get_req.buf_ptr = (u8 *)buf_shm.paddr;
	llcc_cmd_buf->llcc_miss_req.get_req.buf_size = bufsize;
	llcc_cmd_buf->llcc_miss_req.get_req.type = 1;
	llcc_cmd_buf->req_size = sizeof(struct llcc_miss_get_req);
	qtee_shmbridge_flush_shm_buf(&buf_shm);
	ret = send_llcc_miss_profiling_command(llcc_cmd_buf);
	if (ret) {
		pr_err("send_llcc_miss_profiling_command failed\n");
		goto err;
	}

	qtee_shmbridge_inv_shm_buf(&buf_shm);
	memcpy(llcc_miss->data, (char *)buf_shm.vaddr, sizeof(*llcc_miss->data));
	magic = llcc_miss->data->magic;
	if (magic != DDR_OUT_BUF_MAGIC) {
		pr_err("Expected magic value is %x but got %x\n", DDR_OUT_BUF_MAGIC, magic);
		goto err;
	}

	mutex_lock(&llcc_miss->lock);
	for (i = 0; i < MAX_CONCURRENT_MASTERS; i++) {
		m = llcc_miss->data->master_buf[i].master_id;
		if (m >= MAX_MASTER)
			continue;
		mdata[m].miss_data[mdata[m].curr_idx].ts = llcc_miss->data->qtime;
		mdata[m].miss_data[mdata[m].curr_idx].measured_miss_rate =
						llcc_miss->data->master_buf[i].miss_info;
		mdata[m].unread_samples = min(++mdata[m].unread_samples, llcc_miss->max_samples);
		mdata[m].curr_idx = (mdata[m].curr_idx + 1) % llcc_miss->max_samples;
	}

	trace_memory_miss_last_sample(llcc_miss->data->qtime,
			&llcc_miss->data->master_buf[0],
			&llcc_miss->data->master_buf[1]);

	mutex_unlock(&llcc_miss->lock);
err:
	qtee_shmbridge_free_shm(&buf_shm);
	kfree(llcc_cmd_buf);
}

static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer)
{
	ktime_t now = ktime_get();

	queue_work(llcc_miss->wq, &llcc_miss->work);
	hrtimer_forward(timer, now, ms_to_ktime(SAMPLE_MS));

	return HRTIMER_RESTART;
}

static int llcc_miss_create_fs_entries(void)
{
	int i;
	struct dentry *ret, *master_dir;

	llcc_miss_dir = debugfs_create_dir("llcc_miss", 0);
	if (IS_ERR(llcc_miss_dir)) {
		pr_err("Debugfs directory creation failed for llcc_miss\n");
		return PTR_ERR(llcc_miss_dir);
	}

	for (i = 0; i < MAX_MASTER; i++) {
		master_dir = debugfs_create_dir(master_names[i], llcc_miss_dir);
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
		debugfs_remove_recursive(debugfs_lookup(master_names[i], llcc_miss_dir));
	debugfs_remove_recursive(llcc_miss_dir);

	return -ENOENT;
}

static int __init qcom_llcc_miss_init(void)
{
	int ret, i, j;

	llcc_miss =  kzalloc(sizeof(*llcc_miss), GFP_KERNEL);
	if (!llcc_miss)
		return -ENOMEM;
	llcc_miss->data = kzalloc(sizeof(*llcc_miss->data), GFP_KERNEL);
	if (!llcc_miss->data) {
		kfree(llcc_miss);
		return -ENOMEM;
	}
	for (i = 0; i < MAX_MASTER; i++)
		llcc_miss->available_masters |= BIT(i);
	ret =  llcc_miss_create_fs_entries();
	if (ret < 0)
		goto err;

	/*
	 * to get no of hex char in a line multiplying size of struct miss_sample by 2
	 * and adding 1 for tab and 1 for newline.
	 * output format is qtime followed by miss data in hexa,example -> 32c7bd2 a
	 */
	llcc_miss->size_of_line = sizeof(struct miss_sample) * 2 + 2;
	llcc_miss->max_samples = PAGE_SIZE / llcc_miss->size_of_line;
	for (i = 0; i < MAX_MASTER; i++) {
		mdata[i].miss_data = kcalloc(llcc_miss->max_samples,
				sizeof(struct miss_sample), GFP_KERNEL);
		if (!mdata[i].miss_data) {
			ret = -ENOMEM;
			goto debugfs_file_err;
		}
	}

	mutex_init(&llcc_miss->lock);
	hrtimer_init(&llcc_miss->hrtimer, CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
	llcc_miss->hrtimer.function = hrtimer_handler;
	llcc_miss->wq = create_freezable_workqueue("llcc_miss_wq");
	if (!llcc_miss->wq) {
		pr_err("Couldn't create llcc_miss workqueue.\n");
		ret = -ENOMEM;
		goto debugfs_file_err;
	}

	INIT_WORK(&llcc_miss->work, &llcc_miss_update_work);

	return ret;

debugfs_file_err:
	for (j = 0; j < i; j++)
		kfree(mdata[j].miss_data);
	debugfs_remove_recursive(llcc_miss_dir);
err:
	kfree(llcc_miss->data);
	kfree(llcc_miss);

	return ret;
}

module_init(qcom_llcc_miss_init);

MODULE_DESCRIPTION("QCOM LLCC_MISS driver");
MODULE_LICENSE("GPL");
