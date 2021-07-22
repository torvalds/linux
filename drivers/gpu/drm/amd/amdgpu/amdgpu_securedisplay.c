/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "amdgpu_securedisplay.h"

/**
 * DOC: AMDGPU SECUREDISPLAY debugfs test interface
 *
 * how to use?
 * echo opcode <value> > <debugfs_dir>/dri/xxx/securedisplay_test
 * eg. echo 1 > <debugfs_dir>/dri/xxx/securedisplay_test
 * eg. echo 2 phy_id > <debugfs_dir>/dri/xxx/securedisplay_test
 *
 * opcode:
 * 1：Query whether TA is responding used only for validation pupose
 * 2: Send region of Interest and CRC value to I2C. (uint32)phy_id is
 * send to determine which DIO scratch register should be used to get
 * ROI and receive i2c_buf as the output.
 *
 * You can refer more detail from header file ta_securedisplay_if.h
 *
 */

void psp_securedisplay_parse_resp_status(struct psp_context *psp,
	enum ta_securedisplay_status status)
{
	switch (status) {
	case TA_SECUREDISPLAY_STATUS__SUCCESS:
		break;
	case TA_SECUREDISPLAY_STATUS__GENERIC_FAILURE:
		dev_err(psp->adev->dev, "Secure display: Generic Failure.");
		break;
	case TA_SECUREDISPLAY_STATUS__INVALID_PARAMETER:
		dev_err(psp->adev->dev, "Secure display: Invalid Parameter.");
		break;
	case TA_SECUREDISPLAY_STATUS__NULL_POINTER:
		dev_err(psp->adev->dev, "Secure display: Null Pointer.");
		break;
	case TA_SECUREDISPLAY_STATUS__I2C_WRITE_ERROR:
		dev_err(psp->adev->dev, "Secure display: Failed to write to I2C.");
		break;
	case TA_SECUREDISPLAY_STATUS__READ_DIO_SCRATCH_ERROR:
		dev_err(psp->adev->dev, "Secure display: Failed to Read DIO Scratch Register.");
		break;
	case TA_SECUREDISPLAY_STATUS__READ_CRC_ERROR:
		dev_err(psp->adev->dev, "Secure display: Failed to Read CRC");
		break;
	case TA_SECUREDISPLAY_STATUS__I2C_INIT_ERROR:
		dev_err(psp->adev->dev, "Secure display: Failed to initialize I2C.");
		break;
	default:
		dev_err(psp->adev->dev, "Secure display: Failed to parse status: %d\n", status);
	}
}

void psp_prep_securedisplay_cmd_buf(struct psp_context *psp, struct securedisplay_cmd **cmd,
	enum ta_securedisplay_command command_id)
{
	*cmd = (struct securedisplay_cmd *)psp->securedisplay_context.securedisplay_shared_buf;
	memset(*cmd, 0, sizeof(struct securedisplay_cmd));
	(*cmd)->status = TA_SECUREDISPLAY_STATUS__GENERIC_FAILURE;
	(*cmd)->cmd_id = command_id;
}

#if defined(CONFIG_DEBUG_FS)

static ssize_t amdgpu_securedisplay_debugfs_write(struct file *f, const char __user *buf,
		size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct psp_context *psp = &adev->psp;
	struct securedisplay_cmd *securedisplay_cmd;
	struct drm_device *dev = adev_to_drm(adev);
	uint32_t phy_id;
	uint32_t op;
	char str[64];
	int ret;

	if (*pos || size > sizeof(str) - 1)
		return -EINVAL;

	memset(str,  0, sizeof(str));
	ret = copy_from_user(str, buf, size);
	if (ret)
		return -EFAULT;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

	if (size < 3)
		sscanf(str, "%u ", &op);
	else
		sscanf(str, "%u %u", &op, &phy_id);

	switch (op) {
	case 1:
		psp_prep_securedisplay_cmd_buf(psp, &securedisplay_cmd,
			TA_SECUREDISPLAY_COMMAND__QUERY_TA);
		ret = psp_securedisplay_invoke(psp, TA_SECUREDISPLAY_COMMAND__QUERY_TA);
		if (!ret) {
			if (securedisplay_cmd->status == TA_SECUREDISPLAY_STATUS__SUCCESS)
				dev_info(adev->dev, "SECUREDISPLAY: query securedisplay TA ret is 0x%X\n",
					securedisplay_cmd->securedisplay_out_message.query_ta.query_cmd_ret);
			else
				psp_securedisplay_parse_resp_status(psp, securedisplay_cmd->status);
		}
		break;
	case 2:
		psp_prep_securedisplay_cmd_buf(psp, &securedisplay_cmd,
			TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC);
		securedisplay_cmd->securedisplay_in_message.send_roi_crc.phy_id = phy_id;
		ret = psp_securedisplay_invoke(psp, TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC);
		if (!ret) {
			if (securedisplay_cmd->status == TA_SECUREDISPLAY_STATUS__SUCCESS) {
				dev_info(adev->dev, "SECUREDISPLAY: I2C buffer out put is: %*ph\n",
					 TA_SECUREDISPLAY_I2C_BUFFER_SIZE,
					 securedisplay_cmd->securedisplay_out_message.send_roi_crc.i2c_buf);
			} else {
				psp_securedisplay_parse_resp_status(psp, securedisplay_cmd->status);
			}
		}
		break;
	default:
		dev_err(adev->dev, "Invalid input: %s\n", str);
	}

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return size;
}

static const struct file_operations amdgpu_securedisplay_debugfs_ops = {
	.owner = THIS_MODULE,
	.read = NULL,
	.write = amdgpu_securedisplay_debugfs_write,
	.llseek = default_llseek
};

#endif

void amdgpu_securedisplay_debugfs_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)

	if (!adev->psp.securedisplay_context.securedisplay_initialized)
		return;

	debugfs_create_file("securedisplay_test", S_IWUSR, adev_to_drm(adev)->primary->debugfs_root,
				adev, &amdgpu_securedisplay_debugfs_ops);
#endif
}
