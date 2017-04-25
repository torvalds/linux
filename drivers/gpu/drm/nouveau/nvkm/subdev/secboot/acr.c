/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "acr.h"

#include <core/firmware.h>

/**
 * Convenience function to duplicate a firmware file in memory and check that
 * it has the required minimum size.
 */
void *
nvkm_acr_load_firmware(const struct nvkm_subdev *subdev, const char *name,
		       size_t min_size)
{
	const struct firmware *fw;
	void *blob;
	int ret;

	ret = nvkm_firmware_get(subdev->device, name, &fw);
	if (ret)
		return ERR_PTR(ret);
	if (fw->size < min_size) {
		nvkm_error(subdev, "%s is smaller than expected size %zu\n",
			   name, min_size);
		nvkm_firmware_put(fw);
		return ERR_PTR(-EINVAL);
	}
	blob = kmemdup(fw->data, fw->size, GFP_KERNEL);
	nvkm_firmware_put(fw);
	if (!blob)
		return ERR_PTR(-ENOMEM);

	return blob;
}
