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
 * Authors: AMD
 *
 */

#ifndef _DMUB_CMD_DAL_H_
#define _DMUB_CMD_DAL_H_

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_cmd_psr_type {
	DMUB_CMD__PSR_SET_VERSION	= 0,
	DMUB_CMD__PSR_COPY_SETTINGS	= 1,
	DMUB_CMD__PSR_ENABLE		= 2,
	DMUB_CMD__PSR_DISABLE		= 3,
	DMUB_CMD__PSR_SET_LEVEL		= 4,
};

enum psr_version {
	PSR_VERSION_1			= 0x10, // PSR Version 1
	PSR_VERSION_2			= 0x20, // PSR Version 2, includes selective update
	PSR_VERSION_2_1			= 0x21, // PSR Version 2, includes Y-coordinate support for SU
};

enum dmub_cmd_abm_type {
	DMUB_CMD__ABM_INIT_CONFIG	= 0,
	DMUB_CMD__ABM_SET_PIPE		= 1,
	DMUB_CMD__ABM_SET_BACKLIGHT	= 2,
	DMUB_CMD__ABM_SET_LEVEL		= 3,
	DMUB_CMD__ABM_SET_AMBIENT_LEVEL	= 4,
	DMUB_CMD__ABM_SET_PWM_FRAC	= 5,
};

#endif /* _DMUB_CMD_DAL_H_ */
