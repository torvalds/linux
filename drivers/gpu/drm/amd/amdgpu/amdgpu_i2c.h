/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_I2C_H__
#define __AMDGPU_I2C_H__

struct amdgpu_i2c_chan *amdgpu_i2c_create(struct drm_device *dev,
					  const struct amdgpu_i2c_bus_rec *rec,
					  const char *name);
void amdgpu_i2c_destroy(struct amdgpu_i2c_chan *i2c);
void amdgpu_i2c_init(struct amdgpu_device *adev);
void amdgpu_i2c_fini(struct amdgpu_device *adev);
struct amdgpu_i2c_chan *
amdgpu_i2c_lookup(struct amdgpu_device *adev,
		  const struct amdgpu_i2c_bus_rec *i2c_bus);
void
amdgpu_i2c_router_select_ddc_port(const struct amdgpu_connector *connector);
void
amdgpu_i2c_router_select_cd_port(const struct amdgpu_connector *connector);

#endif
