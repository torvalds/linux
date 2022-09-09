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

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_GENERIC_REGS_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_GENERIC_REGS_H_

#include "gpio_regs.h"

#define GENERIC_GPIO_REG_LIST_ENTRY(type, cd, id) \
	.type ## _reg =  REG(DC_GPIO_GENERIC_## type),\
	.type ## _mask =  DC_GPIO_GENERIC_ ## type ## __DC_GPIO_GENERIC ## id ## _ ## type ## _MASK,\
	.type ## _shift = DC_GPIO_GENERIC_ ## type ## __DC_GPIO_GENERIC ## id ## _ ## type ## __SHIFT

#define GENERIC_GPIO_REG_LIST(id) \
	{\
	GENERIC_GPIO_REG_LIST_ENTRY(MASK, cd, id),\
	GENERIC_GPIO_REG_LIST_ENTRY(A, cd, id),\
	GENERIC_GPIO_REG_LIST_ENTRY(EN, cd, id),\
	GENERIC_GPIO_REG_LIST_ENTRY(Y, cd, id)\
	}

#define GENERIC_REG_LIST(id) \
	GENERIC_GPIO_REG_LIST(id), \
	.mux = REG(DC_GENERIC ## id),\

#define GENERIC_MASK_SH_LIST(mask_sh, cd) \
	{(DC_GENERIC ## cd ##__GENERIC ## cd ##_EN## mask_sh),\
	(DC_GENERIC ## cd ##__GENERIC ## cd ##_SEL## mask_sh)}

struct generic_registers {
	struct gpio_registers gpio;
	uint32_t mux;
};

struct generic_sh_mask {
	/* enable */
	uint32_t GENERIC_EN;
	/* select */
	uint32_t GENERIC_SEL;

};


#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_GENERIC_REGS_H_ */
