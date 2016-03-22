/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include "acp_gfx_if.h"

#define ACP_MODE_I2S	0
#define ACP_MODE_AZ	1

#define mmACP_AZALIA_I2S_SELECT 0x51d4

int amd_acp_hw_init(void *cgs_device,
		    unsigned acp_version_major, unsigned acp_version_minor)
{
	unsigned int acp_mode = ACP_MODE_I2S;

	if ((acp_version_major == 2) && (acp_version_minor == 2))
		acp_mode = cgs_read_register(cgs_device,
					mmACP_AZALIA_I2S_SELECT);

	if (acp_mode != ACP_MODE_I2S)
		return -ENODEV;

	return 0;
}
