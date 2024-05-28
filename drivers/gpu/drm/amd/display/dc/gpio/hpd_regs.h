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

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_HPD_REGS_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_HPD_REGS_H_

#include "gpio_regs.h"

#define ONE_MORE_0 1
#define ONE_MORE_1 2
#define ONE_MORE_2 3
#define ONE_MORE_3 4
#define ONE_MORE_4 5
#define ONE_MORE_5 6


#define HPD_GPIO_REG_LIST_ENTRY(type, cd, id) \
	.type ## _reg =  REG(DC_GPIO_HPD_## type),\
	.type ## _mask =  DC_GPIO_HPD_ ## type ## __DC_GPIO_HPD ## id ## _ ## type ## _MASK,\
	.type ## _shift = DC_GPIO_HPD_ ## type ## __DC_GPIO_HPD ## id ## _ ## type ## __SHIFT

#define HPD_GPIO_REG_LIST(id) \
	{\
	HPD_GPIO_REG_LIST_ENTRY(MASK, cd, id),\
	HPD_GPIO_REG_LIST_ENTRY(A, cd, id),\
	HPD_GPIO_REG_LIST_ENTRY(EN, cd, id),\
	HPD_GPIO_REG_LIST_ENTRY(Y, cd, id)\
	}

#define HPD_REG_LIST(id) \
	HPD_GPIO_REG_LIST(ONE_MORE_ ## id), \
	.int_status = REGI(DC_HPD_INT_STATUS, HPD, id),\
	.toggle_filt_cntl = REGI(DC_HPD_TOGGLE_FILT_CNTL, HPD, id)

 #define HPD_MASK_SH_LIST(mask_sh) \
		SF_HPD(DC_HPD_INT_STATUS, DC_HPD_SENSE_DELAYED, mask_sh),\
		SF_HPD(DC_HPD_INT_STATUS, DC_HPD_SENSE, mask_sh),\
		SF_HPD(DC_HPD_TOGGLE_FILT_CNTL, DC_HPD_CONNECT_INT_DELAY, mask_sh),\
		SF_HPD(DC_HPD_TOGGLE_FILT_CNTL, DC_HPD_DISCONNECT_INT_DELAY, mask_sh)

struct hpd_registers {
	struct gpio_registers gpio;
	uint32_t int_status;
	uint32_t toggle_filt_cntl;
};

struct hpd_sh_mask {
	/* int_status */
	uint32_t DC_HPD_SENSE_DELAYED;
	uint32_t DC_HPD_SENSE;
	/* toggle_filt_cntl */
	uint32_t DC_HPD_CONNECT_INT_DELAY;
	uint32_t DC_HPD_DISCONNECT_INT_DELAY;
};


#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_HPD_REGS_H_ */
