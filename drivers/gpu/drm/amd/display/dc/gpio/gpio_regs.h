/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_GPIO_REGS_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_GPIO_REGS_H_

struct gpio_registers {
	uint32_t MASK_reg;
	uint32_t MASK_mask;
	uint32_t MASK_shift;
	uint32_t A_reg;
	uint32_t A_mask;
	uint32_t A_shift;
	uint32_t EN_reg;
	uint32_t EN_mask;
	uint32_t EN_shift;
	uint32_t Y_reg;
	uint32_t Y_mask;
	uint32_t Y_shift;
};


#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_GPIO_REGS_H_ */
