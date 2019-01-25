/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

static int mes_v10_1_add_hw_queue(struct amdgpu_mes *mes,
				  struct mes_add_queue_input *input)
{
	return 0;
}

static int mes_v10_1_remove_hw_queue(struct amdgpu_mes *mes,
				     struct mes_remove_queue_input *input)
{
	return 0;
}

static int mes_v10_1_suspend_gang(struct amdgpu_mes *mes,
				  struct mes_suspend_gang_input *input)
{
	return 0;
}

static int mes_v10_1_resume_gang(struct amdgpu_mes *mes,
				 struct mes_resume_gang_input *input)
{
	return 0;
}

static const struct amdgpu_mes_funcs mes_v10_1_funcs = {
	.add_hw_queue = mes_v10_1_add_hw_queue,
	.remove_hw_queue = mes_v10_1_remove_hw_queue,
	.suspend_gang = mes_v10_1_suspend_gang,
	.resume_gang = mes_v10_1_resume_gang,
};

static int mes_v10_1_sw_init(void *handle)
{
	return 0;
}

static int mes_v10_1_sw_fini(void *handle)
{
	return 0;
}

static int mes_v10_1_hw_init(void *handle)
{
	return 0;
}

static int mes_v10_1_hw_fini(void *handle)
{
	return 0;
}

static int mes_v10_1_suspend(void *handle)
{
	return 0;
}

static int mes_v10_1_resume(void *handle)
{
	return 0;
}

static const struct amd_ip_funcs mes_v10_1_ip_funcs = {
	.name = "mes_v10_1",
	.sw_init = mes_v10_1_sw_init,
	.sw_fini = mes_v10_1_sw_fini,
	.hw_init = mes_v10_1_hw_init,
	.hw_fini = mes_v10_1_hw_fini,
	.suspend = mes_v10_1_suspend,
	.resume = mes_v10_1_resume,
};

const struct amdgpu_ip_block_version mes_v10_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_MES,
	.major = 10,
	.minor = 1,
	.rev = 0,
	.funcs = &mes_v10_1_ip_funcs,
};
