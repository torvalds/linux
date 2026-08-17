// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/iopoll.h>

#include "isp4_debug.h"
#include "isp4_fw_cmd_resp.h"
#include "isp4_hw_reg.h"
#include "isp4_interface.h"

#define ISP4IF_FW_RESP_RB_IRQ_EN_MASK \
	(ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT9_EN_MASK\
	 | ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT12_EN_MASK)

#define ISP4IF_FW_CMD_TIMEOUT (HZ / 2)

struct isp4if_rb_config {
	const char *name;
	u32 index;
	u32 reg_rptr;
	u32 reg_wptr;
	u32 reg_base_lo;
	u32 reg_base_hi;
	u32 reg_size;
	u32 val_size;
	u64 base_mc_addr;
	void *base_sys_addr;
};

/* FW cmd ring buffer configuration */
static struct isp4if_rb_config isp4if_cmd_rb_config[ISP4IF_STREAM_ID_MAX] = {
	{
		.name = "CMD_RB_GBL0",
		.index = 3,
		.reg_rptr = ISP_RB_RPTR4,
		.reg_wptr = ISP_RB_WPTR4,
		.reg_base_lo = ISP_RB_BASE_LO4,
		.reg_base_hi = ISP_RB_BASE_HI4,
		.reg_size = ISP_RB_SIZE4,
	},
	{
		.name = "CMD_RB_STR1",
		.index = 0,
		.reg_rptr = ISP_RB_RPTR1,
		.reg_wptr = ISP_RB_WPTR1,
		.reg_base_lo = ISP_RB_BASE_LO1,
		.reg_base_hi = ISP_RB_BASE_HI1,
		.reg_size = ISP_RB_SIZE1,
	},
	{
		.name = "CMD_RB_STR2",
		.index = 1,
		.reg_rptr = ISP_RB_RPTR2,
		.reg_wptr = ISP_RB_WPTR2,
		.reg_base_lo = ISP_RB_BASE_LO2,
		.reg_base_hi = ISP_RB_BASE_HI2,
		.reg_size = ISP_RB_SIZE2,
	},
	{
		.name = "CMD_RB_STR3",
		.index = 2,
		.reg_rptr = ISP_RB_RPTR3,
		.reg_wptr = ISP_RB_WPTR3,
		.reg_base_lo = ISP_RB_BASE_LO3,
		.reg_base_hi = ISP_RB_BASE_HI3,
		.reg_size = ISP_RB_SIZE3,
	},
};

/* FW resp ring buffer configuration */
static struct isp4if_rb_config isp4if_resp_rb_config[ISP4IF_STREAM_ID_MAX] = {
	{
		.name = "RES_RB_GBL0",
		.index = 3,
		.reg_rptr = ISP_RB_RPTR12,
		.reg_wptr = ISP_RB_WPTR12,
		.reg_base_lo = ISP_RB_BASE_LO12,
		.reg_base_hi = ISP_RB_BASE_HI12,
		.reg_size = ISP_RB_SIZE12,
	},
	{
		.name = "RES_RB_STR1",
		.index = 0,
		.reg_rptr = ISP_RB_RPTR9,
		.reg_wptr = ISP_RB_WPTR9,
		.reg_base_lo = ISP_RB_BASE_LO9,
		.reg_base_hi = ISP_RB_BASE_HI9,
		.reg_size = ISP_RB_SIZE9,
	},
	{
		.name = "RES_RB_STR2",
		.index = 1,
		.reg_rptr = ISP_RB_RPTR10,
		.reg_wptr = ISP_RB_WPTR10,
		.reg_base_lo = ISP_RB_BASE_LO10,
		.reg_base_hi = ISP_RB_BASE_HI10,
		.reg_size = ISP_RB_SIZE10,
	},
	{
		.name = "RES_RB_STR3",
		.index = 2,
		.reg_rptr = ISP_RB_RPTR11,
		.reg_wptr = ISP_RB_WPTR11,
		.reg_base_lo = ISP_RB_BASE_LO11,
		.reg_base_hi = ISP_RB_BASE_HI11,
		.reg_size = ISP_RB_SIZE11,
	},
};

/* FW log ring buffer configuration */
static struct isp4if_rb_config isp4if_log_rb_config = {
	.name = "LOG_RB",
	.index = 0,
	.reg_rptr = ISP_LOG_RB_RPTR0,
	.reg_wptr = ISP_LOG_RB_WPTR0,
	.reg_base_lo = ISP_LOG_RB_BASE_LO0,
	.reg_base_hi = ISP_LOG_RB_BASE_HI0,
	.reg_size = ISP_LOG_RB_SIZE0,
};

static struct isp4if_gpu_mem_info *
isp4if_gpu_mem_alloc(struct isp4_interface *ispif, u32 mem_size)
{
	struct isp4if_gpu_mem_info *mem_info;
	struct device *dev = ispif->dev;
	int ret;

	mem_info = kmalloc_obj(*mem_info, GFP_KERNEL);
	if (!mem_info)
		return NULL;

	mem_info->mem_size = mem_size;
	ret = isp_kernel_buffer_alloc(dev, mem_info->mem_size,
				      &mem_info->mem_handle,
				      &mem_info->gpu_mc_addr,
				      &mem_info->sys_addr);
	if (ret) {
		kfree(mem_info);
		return NULL;
	}

	return mem_info;
}

static void isp4if_gpu_mem_free(struct isp4_interface *ispif,
				struct isp4if_gpu_mem_info **mem_info_ptr)
{
	struct isp4if_gpu_mem_info *mem_info = *mem_info_ptr;
	struct device *dev = ispif->dev;

	if (!mem_info) {
		dev_err(dev, "invalid mem_info\n");
		return;
	}

	*mem_info_ptr = NULL;
	isp_kernel_buffer_free(&mem_info->mem_handle, &mem_info->gpu_mc_addr,
			       &mem_info->sys_addr);
	kfree(mem_info);
}

static void isp4if_dealloc_fw_gpumem(struct isp4_interface *ispif)
{
	isp4if_gpu_mem_free(ispif, &ispif->fw_mem_pool);
	isp4if_gpu_mem_free(ispif, &ispif->fw_cmd_resp_buf);
	isp4if_gpu_mem_free(ispif, &ispif->fw_log_buf);

	for (unsigned int i = 0; i < ISP4IF_MAX_STREAM_BUF_COUNT; i++)
		isp4if_gpu_mem_free(ispif, &ispif->meta_info_buf[i]);
}

static int isp4if_alloc_fw_gpumem(struct isp4_interface *ispif)
{
	struct device *dev = ispif->dev;

	ispif->fw_mem_pool = isp4if_gpu_mem_alloc(ispif,
						  ISP4FW_MEMORY_POOL_SIZE);
	if (!ispif->fw_mem_pool)
		goto error_no_memory;

	ispif->fw_cmd_resp_buf =
		isp4if_gpu_mem_alloc(ispif, ISP4IF_RB_PMBMAP_MEM_SIZE);
	if (!ispif->fw_cmd_resp_buf)
		goto error_no_memory;

	ispif->fw_log_buf =
		isp4if_gpu_mem_alloc(ispif, ISP4IF_FW_LOG_RINGBUF_SIZE);
	if (!ispif->fw_log_buf)
		goto error_no_memory;

	for (unsigned int i = 0; i < ISP4IF_MAX_STREAM_BUF_COUNT; i++) {
		ispif->meta_info_buf[i] =
			isp4if_gpu_mem_alloc(ispif, ISP4IF_META_INFO_BUF_SIZE);
		if (!ispif->meta_info_buf[i])
			goto error_no_memory;
	}

	return 0;

error_no_memory:
	dev_err(dev, "failed to allocate gpu memory\n");
	return -ENOMEM;
}

static u32 isp4if_compute_check_sum(const void *buf, size_t buf_size)
{
	const u8 *surplus_ptr;
	const u32 *buffer;
	u32 checksum = 0;
	size_t i;

	buffer = (const u32 *)buf;
	for (i = 0; i < buf_size / sizeof(u32); i++)
		checksum += buffer[i];

	surplus_ptr = (const u8 *)&buffer[i];
	/* add surplus data crc checksum */
	for (i = 0; i < buf_size % sizeof(u32); i++)
		checksum += surplus_ptr[i];

	return checksum;
}

void isp4if_clear_cmdq(struct isp4_interface *ispif)
{
	struct isp4if_cmd_element *buf_node, *tmp_node;
	LIST_HEAD(free_list);

	scoped_guard(spinlock, &ispif->cmdq_lock)
		list_splice_init(&ispif->cmdq, &free_list);

	list_for_each_entry_safe(buf_node, tmp_node, &free_list, list)
		kfree(buf_node);
}

static bool isp4if_is_cmdq_rb_full(struct isp4_interface *ispif,
				   enum isp4if_stream_id stream)
{
	struct isp4if_rb_config *rb_config = &isp4if_cmd_rb_config[stream];
	u32 rreg = rb_config->reg_rptr, wreg = rb_config->reg_wptr;
	u32 len = rb_config->val_size;
	u32 rd_ptr, wr_ptr;
	u32 bytes_free;

	rd_ptr = isp4hw_rreg(ispif->mmio, rreg);
	wr_ptr = isp4hw_rreg(ispif->mmio, wreg);

	/*
	 * Read and write pointers are equal, indicating the ring buffer
	 * is empty
	 */
	if (wr_ptr == rd_ptr)
		return false;

	if (wr_ptr > rd_ptr)
		bytes_free = len - (wr_ptr - rd_ptr);
	else
		bytes_free = rd_ptr - wr_ptr;

	/*
	 * Ignore one byte from the bytes free to prevent rd_ptr from equaling
	 * wr_ptr when the ring buffer is full, because rd_ptr == wr_ptr is
	 * supposed to indicate that the ring buffer is empty.
	 */
	return bytes_free <= sizeof(struct isp4fw_cmd);
}

struct isp4if_cmd_element *isp4if_rm_cmd_from_cmdq(struct isp4_interface *ispif,
						   u32 seq_num, u32 cmd_id)
{
	struct isp4if_cmd_element *ele;

	guard(spinlock)(&ispif->cmdq_lock);

	list_for_each_entry(ele, &ispif->cmdq, list) {
		if (ele->seq_num == seq_num && ele->cmd_id == cmd_id) {
			list_del(&ele->list);
			return ele;
		}
	}

	return NULL;
}

/* Must check that isp4if_is_cmdq_rb_full() == false before calling */
static int isp4if_insert_isp_fw_cmd(struct isp4_interface *ispif,
				    enum isp4if_stream_id stream,
				    const struct isp4fw_cmd *cmd)
{
	struct isp4if_rb_config *rb_config = &isp4if_cmd_rb_config[stream];
	u32 rreg = rb_config->reg_rptr, wreg = rb_config->reg_wptr;
	void *mem_sys = rb_config->base_sys_addr;
	const u32 cmd_sz = sizeof(*cmd);
	struct device *dev = ispif->dev;
	u32 len = rb_config->val_size;
	const void *src = cmd;
	u32 rd_ptr, wr_ptr;
	u32 bytes_to_end;

	rd_ptr = isp4hw_rreg(ispif->mmio, rreg);
	wr_ptr = isp4hw_rreg(ispif->mmio, wreg);
	if (rd_ptr >= len || wr_ptr >= len) {
		dev_err(dev,
			"rb invalid: stream=%u(%s), rd=%u, wr=%u, len=%u, cmd_sz=%u\n",
			stream, isp4dbg_get_if_stream_str(stream), rd_ptr,
			wr_ptr, len, cmd_sz);
		return -EINVAL;
	}

	bytes_to_end = len - wr_ptr;
	if (bytes_to_end >= cmd_sz) {
		/* FW cmd is just a straight copy to the write pointer */
		memcpy(mem_sys + wr_ptr, src, cmd_sz);
		isp4hw_wreg(ispif->mmio, wreg, (wr_ptr + cmd_sz) % len);
	} else {
		/*
		 * FW cmd is split because the ring buffer needs to wrap
		 * around
		 */
		memcpy(mem_sys + wr_ptr, src, bytes_to_end);
		memcpy(mem_sys, src + bytes_to_end, cmd_sz - bytes_to_end);
		isp4hw_wreg(ispif->mmio, wreg, cmd_sz - bytes_to_end);
	}

	return 0;
}

static inline enum isp4if_stream_id isp4if_get_fw_stream(u32 cmd_id)
{
	return ISP4IF_STREAM_ID_1;
}

static int isp4if_send_fw_cmd(struct isp4_interface *ispif, u32 cmd_id,
			      const void *package,
			      u32 package_size, bool sync)
{
	enum isp4if_stream_id stream = isp4if_get_fw_stream(cmd_id);
	struct isp4if_cmd_element *ele = NULL;
	struct device *dev = ispif->dev;
	struct isp4fw_cmd cmd;
	u32 seq_num;
	int ret;

	if (package_size > sizeof(cmd.cmd_param)) {
		dev_err(dev, "fail pkgsize(%u) > %zu cmd:0x%x, stream %d\n",
			package_size, sizeof(cmd.cmd_param), cmd_id, stream);
		return -EINVAL;
	}

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd_id = cmd_id;
	switch (stream) {
	case ISP4IF_STREAM_ID_GLOBAL:
		cmd.cmd_stream_id = ISP4FW_STREAM_ID_INVALID;
		break;
	case ISP4IF_STREAM_ID_1:
		cmd.cmd_stream_id = ISP4FW_STREAM_ID_1;
		break;
	default:
		dev_err(dev, "fail bad stream id %d\n", stream);
		return -EINVAL;
	}

	/* Allocate the sync command object early and outside of the lock */
	if (sync) {
		ele = kmalloc_obj(*ele, GFP_KERNEL);
		if (!ele)
			return -ENOMEM;

		/* Get two references: one for the resp thread, one for us */
		atomic_set(&ele->refcnt, 2);
		init_completion(&ele->cmd_done);
	}

	if (package && package_size)
		memcpy(cmd.cmd_param, package, package_size);

	scoped_guard(mutex, &ispif->isp4if_mutex) {
		ret = read_poll_timeout(isp4if_is_cmdq_rb_full, ret, !ret,
					ISP4IF_RB_FULL_SLEEP_US,
					ISP4IF_RB_FULL_TIMEOUT_US, false, ispif,
					stream);
		if (ret) {
			struct isp4if_rb_config *rb_config =
					&isp4if_resp_rb_config[stream];
			u32 rd_ptr = isp4hw_rreg(ispif->mmio,
						 rb_config->reg_rptr);
			u32 wr_ptr = isp4hw_rreg(ispif->mmio,
						 rb_config->reg_wptr);

			dev_err(dev,
				"failed to get free cmdq slot, stream %s(%d),rd %u, wr %u\n",
				isp4dbg_get_if_stream_str(stream), stream,
				rd_ptr, wr_ptr);
			ret = -ETIMEDOUT;
			goto free_ele;
		}

		seq_num = ispif->host2fw_seq_num++;
		cmd.cmd_seq_num = seq_num;
		cmd.cmd_check_sum = isp4if_compute_check_sum(&cmd, sizeof(cmd)
							     - sizeof(u32));

		/*
		 * only append the fw cmd to queue when its response needs to
		 * be waited for, currently there are only two such commands,
		 * disable channel and stop stream which are only sent after
		 * close camera
		 */
		if (ele) {
			ele->seq_num = seq_num;
			ele->cmd_id = cmd_id;
			scoped_guard(spinlock, &ispif->cmdq_lock)
				list_add_tail(&ele->list, &ispif->cmdq);
		}

		ret = isp4if_insert_isp_fw_cmd(ispif, stream, &cmd);
		if (ret) {
			dev_err(dev,
				"fail for insert_isp_fw_cmd cmd_id %s(0x%08x)\n",
				isp4dbg_get_cmd_str(cmd_id), cmd_id);
			goto err_dequeue_ele;
		}
	}

	if (ele) {
		ret = wait_for_completion_timeout(&ele->cmd_done,
						  ISP4IF_FW_CMD_TIMEOUT);
		if (!ret) {
			ret = -ETIMEDOUT;
			goto err_dequeue_ele;
		}

		ret = 0;
		goto put_ele_ref;
	}

	return 0;

err_dequeue_ele:
	/*
	 * Try to remove the command from the queue. If that fails, then it
	 * means the response thread is currently using the object, and we need
	 * to use the refcount to avoid a use-after-free by either side.
	 */
	if (ele && isp4if_rm_cmd_from_cmdq(ispif, seq_num, cmd_id))
		goto free_ele;

put_ele_ref:
	/* Don't free the command if we didn't put the last reference */
	if (ele && atomic_dec_return(&ele->refcnt))
		ele = NULL;

free_ele:
	kfree(ele);
	return ret;
}

static int isp4if_send_buffer(struct isp4_interface *ispif,
			      struct isp4if_img_buf_info *buf_info)
{
	struct isp4fw_cmd_send_buffer cmd;

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.buffer_type = ISP4FW_BUFFER_TYPE_PREVIEW;
	cmd.buffer.vmid_space.bit.space = ISP4FW_ADDR_SPACE_TYPE_GPU_VA;
	isp4if_split_addr64(buf_info->planes[0].mc_addr,
			    &cmd.buffer.buf_base_a_lo,
			    &cmd.buffer.buf_base_a_hi);
	cmd.buffer.buf_size_a = buf_info->planes[0].len;

	isp4if_split_addr64(buf_info->planes[1].mc_addr,
			    &cmd.buffer.buf_base_b_lo,
			    &cmd.buffer.buf_base_b_hi);
	cmd.buffer.buf_size_b = buf_info->planes[1].len;

	isp4if_split_addr64(buf_info->planes[2].mc_addr,
			    &cmd.buffer.buf_base_c_lo,
			    &cmd.buffer.buf_base_c_hi);
	cmd.buffer.buf_size_c = buf_info->planes[2].len;

	return isp4if_send_fw_cmd(ispif, ISP4FW_CMD_ID_SEND_BUFFER, &cmd,
				  sizeof(cmd), false);
}

static void isp4if_init_rb_config(struct isp4_interface *ispif,
				  struct isp4if_rb_config *rb_config)
{
	isp4hw_wreg(ispif->mmio, rb_config->reg_rptr, 0x0);
	isp4hw_wreg(ispif->mmio, rb_config->reg_wptr, 0x0);
	isp4hw_wreg(ispif->mmio, rb_config->reg_base_lo,
		    rb_config->base_mc_addr);
	isp4hw_wreg(ispif->mmio, rb_config->reg_base_hi,
		    rb_config->base_mc_addr >> 32);
	isp4hw_wreg(ispif->mmio, rb_config->reg_size, rb_config->val_size);
}

static int isp4if_fw_init(struct isp4_interface *ispif)
{
	u32 aligned_rb_chunk_size = ISP4IF_RB_PMBMAP_MEM_CHUNK & 0xffffffc0;
	struct isp4if_rb_config *rb_config;
	u32 offset;
	unsigned int i;

	/* initialize CMD_RB streams */
	for (i = 0; i < ISP4IF_STREAM_ID_MAX; i++) {
		rb_config = (isp4if_cmd_rb_config + i);
		offset = aligned_rb_chunk_size * rb_config->index;

		rb_config->val_size = ISP4IF_FW_CMD_BUF_SIZE;
		rb_config->base_sys_addr =
			ispif->fw_cmd_resp_buf->sys_addr + offset;
		rb_config->base_mc_addr =
			ispif->fw_cmd_resp_buf->gpu_mc_addr + offset;

		isp4if_init_rb_config(ispif, rb_config);
	}

	/* initialize RESP_RB streams */
	for (i = 0; i < ISP4IF_STREAM_ID_MAX; i++) {
		rb_config = (isp4if_resp_rb_config + i);
		offset = aligned_rb_chunk_size *
			 (rb_config->index + ISP4IF_RESP_CHAN_TO_RB_OFFSET - 1);

		rb_config->val_size = ISP4IF_FW_CMD_BUF_SIZE;
		rb_config->base_sys_addr =
			ispif->fw_cmd_resp_buf->sys_addr + offset;
		rb_config->base_mc_addr =
			ispif->fw_cmd_resp_buf->gpu_mc_addr + offset;

		isp4if_init_rb_config(ispif, rb_config);
	}

	/* initialize LOG_RB stream */
	rb_config = &isp4if_log_rb_config;
	rb_config->val_size = ISP4IF_FW_LOG_RINGBUF_SIZE;
	rb_config->base_mc_addr = ispif->fw_log_buf->gpu_mc_addr;
	rb_config->base_sys_addr = ispif->fw_log_buf->sys_addr;

	isp4if_init_rb_config(ispif, rb_config);

	return 0;
}

static int isp4if_wait_fw_ready(struct isp4_interface *ispif,
				u32 isp_status_addr)
{
	struct device *dev = ispif->dev;
	u32 timeout_ms = 100;
	u32 interval_ms = 1;
	u32 reg_val;

	/* wait for FW initialize done! */
	if (!read_poll_timeout(isp4hw_rreg, reg_val, reg_val
			       & ISP_STATUS__CCPU_REPORT_MASK,
			       interval_ms * 1000, timeout_ms * 1000, false,
			       ispif->mmio, isp_status_addr))
		return 0;

	dev_err(dev, "ISP CCPU FW boot failed\n");

	return -ETIME;
}

static void isp4if_enable_ccpu(struct isp4_interface *ispif)
{
	u32 reg_val;

	reg_val = isp4hw_rreg(ispif->mmio, ISP_SOFT_RESET);
	reg_val &= (~ISP_SOFT_RESET__CCPU_SOFT_RESET_MASK);
	isp4hw_wreg(ispif->mmio, ISP_SOFT_RESET, reg_val);

	usleep_range(100, 150);

	reg_val = isp4hw_rreg(ispif->mmio, ISP_CCPU_CNTL);
	reg_val &= (~ISP_CCPU_CNTL__CCPU_HOST_SOFT_RST_MASK);
	isp4hw_wreg(ispif->mmio, ISP_CCPU_CNTL, reg_val);
}

static void isp4if_disable_ccpu(struct isp4_interface *ispif)
{
	u32 reg_val;

	reg_val = isp4hw_rreg(ispif->mmio, ISP_CCPU_CNTL);
	reg_val |= ISP_CCPU_CNTL__CCPU_HOST_SOFT_RST_MASK;
	isp4hw_wreg(ispif->mmio, ISP_CCPU_CNTL, reg_val);

	usleep_range(100, 150);

	reg_val = isp4hw_rreg(ispif->mmio, ISP_SOFT_RESET);
	reg_val |= ISP_SOFT_RESET__CCPU_SOFT_RESET_MASK;
	isp4hw_wreg(ispif->mmio, ISP_SOFT_RESET, reg_val);
}

static int isp4if_fw_boot(struct isp4_interface *ispif)
{
	struct device *dev = ispif->dev;

	if (ispif->status != ISP4IF_STATUS_PWR_ON) {
		dev_err(dev, "invalid isp power status %d\n", ispif->status);
		return -EINVAL;
	}

	isp4if_disable_ccpu(ispif);

	isp4if_fw_init(ispif);

	/* clear ccpu status */
	isp4hw_wreg(ispif->mmio, ISP_STATUS, 0x0);

	isp4if_enable_ccpu(ispif);

	if (isp4if_wait_fw_ready(ispif, ISP_STATUS)) {
		isp4if_disable_ccpu(ispif);
		return -EINVAL;
	}

	/* enable interrupts */
	isp4hw_wreg(ispif->mmio, ISP_SYS_INT0_EN,
		    ISP4IF_FW_RESP_RB_IRQ_EN_MASK);

	ispif->status = ISP4IF_STATUS_FW_RUNNING;

	dev_dbg(dev, "ISP CCPU FW boot success\n");

	return 0;
}

int isp4if_f2h_resp(struct isp4_interface *ispif, enum isp4if_stream_id stream,
		    struct isp4fw_resp *resp)
{
	struct isp4if_rb_config *rb_config = &isp4if_resp_rb_config[stream];
	u32 rreg = rb_config->reg_rptr, wreg = rb_config->reg_wptr;
	void *mem_sys = rb_config->base_sys_addr;
	const u32 resp_sz = sizeof(*resp);
	struct device *dev = ispif->dev;
	u32 len = rb_config->val_size;
	u32 rd_ptr, wr_ptr;
	u32 bytes_to_end;
	void *dst = resp;
	u32 checksum;

	rd_ptr = isp4hw_rreg(ispif->mmio, rreg);
	wr_ptr = isp4hw_rreg(ispif->mmio, wreg);
	if (rd_ptr >= len || wr_ptr >= len)
		goto err_rb_invalid;

	/*
	 * Read and write pointers are equal, indicating the ring buffer is
	 * empty
	 */
	if (rd_ptr == wr_ptr)
		return -ENODATA;

	bytes_to_end = len - rd_ptr;
	if (bytes_to_end >= resp_sz) {
		/* FW response is just a straight copy from the read pointer */
		if (wr_ptr > rd_ptr && wr_ptr - rd_ptr < resp_sz)
			goto err_rb_invalid;

		memcpy(dst, mem_sys + rd_ptr, resp_sz);
		isp4hw_wreg(ispif->mmio, rreg, (rd_ptr + resp_sz) % len);
	} else {
		/*
		 * FW response is split because the ring buffer wrapped
		 * around
		 */
		if (wr_ptr > rd_ptr || wr_ptr < resp_sz - bytes_to_end)
			goto err_rb_invalid;

		memcpy(dst, mem_sys + rd_ptr, bytes_to_end);
		memcpy(dst + bytes_to_end, mem_sys, resp_sz - bytes_to_end);
		isp4hw_wreg(ispif->mmio, rreg, resp_sz - bytes_to_end);
	}

	checksum = isp4if_compute_check_sum(resp, resp_sz - sizeof(u32));
	if (checksum != resp->resp_check_sum) {
		dev_err(dev, "resp checksum 0x%x,should 0x%x,rptr %u,wptr %u\n",
			checksum, resp->resp_check_sum, rd_ptr, wr_ptr);
		dev_err(dev, "%s(%u), seqNo %u, resp_id %s(0x%x)\n",
			isp4dbg_get_if_stream_str(stream), stream,
			resp->resp_seq_num, isp4dbg_get_resp_str(resp->resp_id),
			resp->resp_id);
		return -EINVAL;
	}

	return 0;

err_rb_invalid:
	dev_err(dev,
		"rb invalid: stream=%u(%s), rd=%u, wr=%u, len=%u, resp_sz=%u\n",
		stream, isp4dbg_get_if_stream_str(stream), rd_ptr, wr_ptr, len,
		resp_sz);
	return -EINVAL;
}

int isp4if_send_command(struct isp4_interface *ispif, u32 cmd_id,
			const void *package, u32 package_size)
{
	return isp4if_send_fw_cmd(ispif, cmd_id, package, package_size, false);
}

int isp4if_send_command_sync(struct isp4_interface *ispif, u32 cmd_id,
			     const void *package, u32 package_size)
{
	return isp4if_send_fw_cmd(ispif, cmd_id, package, package_size, true);
}

void isp4if_clear_bufq(struct isp4_interface *ispif)
{
	struct isp4if_img_buf_node *buf_node, *tmp_node;
	LIST_HEAD(free_list);

	scoped_guard(spinlock, &ispif->bufq_lock)
		list_splice_init(&ispif->bufq, &free_list);

	list_for_each_entry_safe(buf_node, tmp_node, &free_list, node)
		kfree(buf_node);
}

void isp4if_dealloc_buffer_node(struct isp4if_img_buf_node *buf_node)
{
	kfree(buf_node);
}

struct isp4if_img_buf_node *
isp4if_alloc_buffer_node(struct isp4if_img_buf_info *buf_info)
{
	struct isp4if_img_buf_node *node;

	node = kmalloc_obj(*node, GFP_KERNEL);
	if (node)
		node->buf_info = *buf_info;

	return node;
}

struct isp4if_img_buf_node *isp4if_dequeue_buffer(struct isp4_interface *ispif)
{
	struct isp4if_img_buf_node *buf_node;

	guard(spinlock)(&ispif->bufq_lock);

	buf_node = list_first_entry_or_null(&ispif->bufq, typeof(*buf_node),
					    node);
	if (buf_node)
		list_del(&buf_node->node);

	return buf_node;
}

int isp4if_queue_buffer(struct isp4_interface *ispif,
			struct isp4if_img_buf_node *buf_node)
{
	int ret;

	ret = isp4if_send_buffer(ispif, &buf_node->buf_info);
	if (ret)
		return ret;

	scoped_guard(spinlock, &ispif->bufq_lock)
		list_add_tail(&buf_node->node, &ispif->bufq);

	return 0;
}

int isp4if_stop(struct isp4_interface *ispif)
{
	isp4if_disable_ccpu(ispif);

	isp4if_dealloc_fw_gpumem(ispif);

	return 0;
}

int isp4if_start(struct isp4_interface *ispif)
{
	int ret;

	ret = isp4if_alloc_fw_gpumem(ispif);
	if (ret)
		return ret;

	ret = isp4if_fw_boot(ispif);
	if (ret)
		goto failed_fw_boot;

	return 0;

failed_fw_boot:
	isp4if_dealloc_fw_gpumem(ispif);
	return ret;
}

int isp4if_deinit(struct isp4_interface *ispif)
{
	isp4if_clear_cmdq(ispif);

	isp4if_clear_bufq(ispif);

	mutex_destroy(&ispif->isp4if_mutex);

	return 0;
}

int isp4if_init(struct isp4_interface *ispif, struct device *dev,
		void __iomem *isp_mmio)
{
	ispif->dev = dev;
	ispif->mmio = isp_mmio;

	spin_lock_init(&ispif->cmdq_lock); /* used for cmdq access */
	spin_lock_init(&ispif->bufq_lock); /* used for bufq access */
	mutex_init(&ispif->isp4if_mutex); /* used for commands sent to ispfw */

	INIT_LIST_HEAD(&ispif->cmdq);
	INIT_LIST_HEAD(&ispif->bufq);

	return 0;
}
