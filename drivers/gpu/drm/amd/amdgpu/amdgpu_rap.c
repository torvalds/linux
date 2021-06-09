/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 *
 */
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>

#include "amdgpu.h"
#include "amdgpu_rap.h"

/**
 * DOC: AMDGPU RAP debugfs test interface
 *
 * how to use?
 * echo opcode > <debugfs_dir>/dri/xxx/rap_test
 *
 * opcode:
 * currently, only 2 is supported by Linux host driver,
 * opcode 2 stands for TA_CMD_RAP__VALIDATE_L0, used to
 * trigger L0 policy validation, you can refer more detail
 * from header file ta_rap_if.h
 *
 */
static ssize_t amdgpu_rap_debugfs_write(struct file *f, const char __user *buf,
		size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct ta_rap_shared_memory *rap_shared_mem;
	struct ta_rap_cmd_output_data *rap_cmd_output;
	struct drm_device *dev = adev_to_drm(adev);
	uint32_t op;
	enum ta_rap_status status;
	int ret;

	if (*pos || size != 2)
		return -EINVAL;

	ret = kstrtouint_from_user(buf, size, *pos, &op);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

	/* make sure gfx core is on, RAP TA cann't handle
	 * GFX OFF case currently.
	 */
	amdgpu_gfx_off_ctrl(adev, false);

	switch (op) {
	case 2:
		ret = psp_rap_invoke(&adev->psp, op, &status);
		if (!ret && status == TA_RAP_STATUS__SUCCESS) {
			dev_info(adev->dev, "RAP L0 validate test success.\n");
		} else {
			rap_shared_mem = (struct ta_rap_shared_memory *)
					 adev->psp.rap_context.rap_shared_buf;
			rap_cmd_output = &(rap_shared_mem->rap_out_message.output);

			dev_info(adev->dev, "RAP test failed, the output is:\n");
			dev_info(adev->dev, "\tlast_subsection: 0x%08x.\n",
				 rap_cmd_output->last_subsection);
			dev_info(adev->dev, "\tnum_total_validate: 0x%08x.\n",
				 rap_cmd_output->num_total_validate);
			dev_info(adev->dev, "\tnum_valid: 0x%08x.\n",
				 rap_cmd_output->num_valid);
			dev_info(adev->dev, "\tlast_validate_addr: 0x%08x.\n",
				 rap_cmd_output->last_validate_addr);
			dev_info(adev->dev, "\tlast_validate_val: 0x%08x.\n",
				 rap_cmd_output->last_validate_val);
			dev_info(adev->dev, "\tlast_validate_val_exptd: 0x%08x.\n",
				 rap_cmd_output->last_validate_val_exptd);
		}
		break;
	default:
		dev_info(adev->dev, "Unsupported op id: %d, ", op);
		dev_info(adev->dev, "Only support op 2(L0 validate test).\n");
		break;
	}

	amdgpu_gfx_off_ctrl(adev, true);
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return size;
}

static const struct file_operations amdgpu_rap_debugfs_ops = {
	.owner = THIS_MODULE,
	.read = NULL,
	.write = amdgpu_rap_debugfs_write,
	.llseek = default_llseek
};

void amdgpu_rap_debugfs_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;

	if (!adev->psp.rap_context.rap_initialized)
		return;

	debugfs_create_file("rap_test", S_IWUSR, minor->debugfs_root,
				adev, &amdgpu_rap_debugfs_ops);
#endif
}
