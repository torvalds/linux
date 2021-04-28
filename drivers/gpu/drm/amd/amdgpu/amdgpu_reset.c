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
 */

#include "amdgpu_reset.h"
#include "aldebaran.h"

int amdgpu_reset_add_handler(struct amdgpu_reset_control *reset_ctl,
			     struct amdgpu_reset_handler *handler)
{
	/* TODO: Check if handler exists? */
	list_add_tail(&handler->handler_list, &reset_ctl->reset_handlers);
	return 0;
}

int amdgpu_reset_init(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_ALDEBARAN:
		ret = aldebaran_reset_init(adev);
		break;
	default:
		break;
	}

	return ret;
}

int amdgpu_reset_fini(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_ALDEBARAN:
		ret = aldebaran_reset_fini(adev);
		break;
	default:
		break;
	}

	return ret;
}

int amdgpu_reset_prepare_hwcontext(struct amdgpu_device *adev,
				   struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_reset_handler *reset_handler = NULL;

	if (adev->reset_cntl && adev->reset_cntl->get_reset_handler)
		reset_handler = adev->reset_cntl->get_reset_handler(
			adev->reset_cntl, reset_context);
	if (!reset_handler)
		return -ENOSYS;

	return reset_handler->prepare_hwcontext(adev->reset_cntl,
						reset_context);
}

int amdgpu_reset_perform_reset(struct amdgpu_device *adev,
			       struct amdgpu_reset_context *reset_context)
{
	int ret;
	struct amdgpu_reset_handler *reset_handler = NULL;

	if (adev->reset_cntl)
		reset_handler = adev->reset_cntl->get_reset_handler(
			adev->reset_cntl, reset_context);
	if (!reset_handler)
		return -ENOSYS;

	ret = reset_handler->perform_reset(adev->reset_cntl, reset_context);
	if (ret)
		return ret;

	return reset_handler->restore_hwcontext(adev->reset_cntl,
						reset_context);
}
