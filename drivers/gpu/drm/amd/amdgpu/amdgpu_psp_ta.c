/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "amdgpu.h"
#include "amdgpu_psp_ta.h"

#if defined(CONFIG_DEBUG_FS)

static ssize_t ta_if_load_debugfs_write(struct file *fp, const char *buf,
					    size_t len, loff_t *off);
static ssize_t ta_if_unload_debugfs_write(struct file *fp, const char *buf,
					    size_t len, loff_t *off);
static ssize_t ta_if_invoke_debugfs_write(struct file *fp, const char *buf,
					    size_t len, loff_t *off);

static uint32_t get_bin_version(const uint8_t *bin)
{
	const struct common_firmware_header *hdr =
			     (const struct common_firmware_header *)bin;

	return hdr->ucode_version;
}

static void prep_ta_mem_context(struct psp_context *psp,
					     struct ta_context *context,
					     uint8_t *shared_buf,
					     uint32_t shared_buf_len)
{
	context->mem_context.shared_mem_size = PAGE_ALIGN(shared_buf_len);
	psp_ta_init_shared_buf(psp, &context->mem_context);

	memcpy((void *)context->mem_context.shared_buf, shared_buf, shared_buf_len);
}

static bool is_ta_type_valid(enum ta_type_id ta_type)
{
	bool ret = false;

	switch (ta_type) {
	case TA_TYPE_RAS:
		ret = true;
		break;
	default:
		break;
	}

	return ret;
}

static const struct file_operations ta_load_debugfs_fops = {
	.write  = ta_if_load_debugfs_write,
	.llseek = default_llseek,
	.owner  = THIS_MODULE
};

static const struct file_operations ta_unload_debugfs_fops = {
	.write  = ta_if_unload_debugfs_write,
	.llseek = default_llseek,
	.owner  = THIS_MODULE
};

static const struct file_operations ta_invoke_debugfs_fops = {
	.write  = ta_if_invoke_debugfs_write,
	.llseek = default_llseek,
	.owner  = THIS_MODULE
};


/**
 * DOC: AMDGPU TA debugfs interfaces
 *
 * Three debugfs interfaces can be opened by a program to
 * load/invoke/unload TA,
 *
 * - /sys/kernel/debug/dri/<N>/ta_if/ta_load
 * - /sys/kernel/debug/dri/<N>/ta_if/ta_invoke
 * - /sys/kernel/debug/dri/<N>/ta_if/ta_unload
 *
 * How to use the interfaces in a program?
 *
 * A program needs to provide transmit buffer to the interfaces
 * and will receive buffer from the interfaces below,
 *
 * - For TA load debugfs interface:
 *   Transmit buffer:
 *    - TA type (4bytes)
 *    - TA bin length (4bytes)
 *    - TA bin
 *   Receive buffer:
 *    - TA ID (4bytes)
 *
 * - For TA invoke debugfs interface:
 *   Transmit buffer:
 *    - TA ID (4bytes)
 *    - TA CMD ID (4bytes)
 *    - TA shard buf length (4bytes)
 *    - TA shared buf
 *   Receive buffer:
 *    - TA shared buf
 *
 * - For TA unload debugfs interface:
 *   Transmit buffer:
 *    - TA ID (4bytes)
 */

static ssize_t ta_if_load_debugfs_write(struct file *fp, const char *buf, size_t len, loff_t *off)
{
	uint32_t ta_type    = 0;
	uint32_t ta_bin_len = 0;
	uint8_t  *ta_bin    = NULL;
	uint32_t copy_pos   = 0;
	int      ret        = 0;

	struct amdgpu_device *adev   = (struct amdgpu_device *)file_inode(fp)->i_private;
	struct psp_context   *psp    = &adev->psp;
	struct ta_context    context = {0};

	if (!buf)
		return -EINVAL;

	ret = copy_from_user((void *)&ta_type, &buf[copy_pos], sizeof(uint32_t));
	if (ret || (!is_ta_type_valid(ta_type)))
		return -EINVAL;

	copy_pos += sizeof(uint32_t);

	ret = copy_from_user((void *)&ta_bin_len, &buf[copy_pos], sizeof(uint32_t));
	if (ret)
		return -EINVAL;

	copy_pos += sizeof(uint32_t);

	ta_bin = kzalloc(ta_bin_len, GFP_KERNEL);
	if (!ta_bin)
		ret = -ENOMEM;
	if (copy_from_user((void *)ta_bin, &buf[copy_pos], ta_bin_len)) {
		ret = -EFAULT;
		goto err_free_bin;
	}

	ret = psp_ras_terminate(psp);
	if (ret) {
		dev_err(adev->dev, "Failed to unload embedded RAS TA\n");
		goto err_free_bin;
	}

	context.ta_type             = ta_type;
	context.ta_load_type        = GFX_CMD_ID_LOAD_TA;
	context.bin_desc.fw_version = get_bin_version(ta_bin);
	context.bin_desc.size_bytes = ta_bin_len;
	context.bin_desc.start_addr = ta_bin;

	ret = psp_ta_load(psp, &context);

	if (ret || context.resp_status) {
		dev_err(adev->dev, "TA load via debugfs failed (%d) status %d\n",
			 ret, context.resp_status);
		if (!ret)
			ret = -EINVAL;
		goto err_free_bin;
	}

	context.initialized = true;
	if (copy_to_user((char *)buf, (void *)&context.session_id, sizeof(uint32_t)))
		ret = -EFAULT;

err_free_bin:
	kfree(ta_bin);

	return ret;
}

static ssize_t ta_if_unload_debugfs_write(struct file *fp, const char *buf, size_t len, loff_t *off)
{
	uint32_t ta_id  = 0;
	int      ret    = 0;

	struct amdgpu_device *adev   = (struct amdgpu_device *)file_inode(fp)->i_private;
	struct psp_context   *psp    = &adev->psp;
	struct ta_context    context = {0};

	if (!buf)
		return -EINVAL;

	ret = copy_from_user((void *)&ta_id, buf, sizeof(uint32_t));
	if (ret)
		return -EINVAL;

	context.session_id = ta_id;

	ret = psp_ta_unload(psp, &context);
	if (!ret)
		context.initialized = false;

	return ret;
}

static ssize_t ta_if_invoke_debugfs_write(struct file *fp, const char *buf, size_t len, loff_t *off)
{
	uint32_t ta_id          = 0;
	uint32_t cmd_id         = 0;
	uint32_t shared_buf_len = 0;
	uint8_t	 *shared_buf    = NULL;
	uint32_t copy_pos       = 0;
	int      ret            = 0;

	struct amdgpu_device *adev   = (struct amdgpu_device *)file_inode(fp)->i_private;
	struct psp_context   *psp    = &adev->psp;
	struct ta_context    context = {0};

	if (!buf)
		return -EINVAL;

	ret = copy_from_user((void *)&ta_id, &buf[copy_pos], sizeof(uint32_t));
	if (ret)
		return -EINVAL;
	copy_pos += sizeof(uint32_t);

	ret = copy_from_user((void *)&cmd_id, &buf[copy_pos], sizeof(uint32_t));
	if (ret)
		return -EINVAL;
	copy_pos += sizeof(uint32_t);

	ret = copy_from_user((void *)&shared_buf_len, &buf[copy_pos], sizeof(uint32_t));
	if (ret)
		return -EINVAL;
	copy_pos += sizeof(uint32_t);

	shared_buf = kzalloc(shared_buf_len, GFP_KERNEL);
	if (!shared_buf)
		return -ENOMEM;
	if (copy_from_user((void *)shared_buf, &buf[copy_pos], shared_buf_len)) {
		ret = -EFAULT;
		goto err_free_shared_buf;
	}

	context.session_id = ta_id;

	prep_ta_mem_context(psp, &context, shared_buf, shared_buf_len);

	ret = psp_ta_invoke_indirect(psp, cmd_id, &context);

	if (ret || context.resp_status) {
		dev_err(adev->dev, "TA invoke via debugfs failed (%d) status %d\n",
			 ret, context.resp_status);
		if (!ret)
			ret = -EINVAL;
		goto err_free_ta_shared_buf;
	}

	if (copy_to_user((char *)buf, context.mem_context.shared_buf, shared_buf_len))
		ret = -EFAULT;

err_free_ta_shared_buf:
	psp_ta_free_shared_buf(&context.mem_context);

err_free_shared_buf:
	kfree(shared_buf);

	return ret;
}

void amdgpu_ta_if_debugfs_init(struct amdgpu_device *adev)
{
	struct drm_minor *minor = adev_to_drm(adev)->primary;

	struct dentry *dir = debugfs_create_dir("ta_if", minor->debugfs_root);

	debugfs_create_file("ta_load", 0200, dir, adev,
				     &ta_load_debugfs_fops);

	debugfs_create_file("ta_unload", 0200, dir,
				     adev, &ta_unload_debugfs_fops);

	debugfs_create_file("ta_invoke", 0200, dir,
				     adev, &ta_invoke_debugfs_fops);
}

#else
void amdgpu_ta_if_debugfs_init(struct amdgpu_device *adev)
{

}
#endif
