/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "smuio_v13_0_3.h"
#include "soc15_common.h"
#include "smuio/smuio_13_0_3_offset.h"
#include "smuio/smuio_13_0_3_sh_mask.h"

#define PKG_TYPE_MASK		0x00000003L

/**
 * smuio_v13_0_3_get_die_id - query die id from FCH.
 *
 * @adev: amdgpu device pointer
 *
 * Returns die id
 */
static u32 smuio_v13_0_3_get_die_id(struct amdgpu_device *adev)
{
	u32 data, die_id;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	die_id = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, DIE_ID);

	return die_id;
}

/**
 * smuio_v13_0_3_get_socket_id - query socket id from FCH
 *
 * @adev: amdgpu device pointer
 *
 * Returns socket id
 */
static u32 smuio_v13_0_3_get_socket_id(struct amdgpu_device *adev)
{
	u32 data, socket_id;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	socket_id = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, SOCKET_ID);

	return socket_id;
}

/**
 * smuio_v13_0_3_get_pkg_type - query package type set by MP1/bootcode
 *
 * @adev: amdgpu device pointer
 *
 * Returns package type
 */

static enum amdgpu_pkg_type smuio_v13_0_3_get_pkg_type(struct amdgpu_device *adev)
{
	enum amdgpu_pkg_type pkg_type;
	u32 data;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	data = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, PKG_TYPE);
	/* pkg_type[4:0]
	 *
	 * bit 1 == 1 APU form factor
	 *
	 * b0100 - b1111 - Reserved
	 */
	switch (data & PKG_TYPE_MASK) {
	case 0x0:
		pkg_type = AMDGPU_PKG_TYPE_CEM;
		break;
	case 0x1:
		pkg_type = AMDGPU_PKG_TYPE_OAM;
		break;
	case 0x2:
		pkg_type = AMDGPU_PKG_TYPE_APU;
		break;
	default:
		pkg_type = AMDGPU_PKG_TYPE_UNKNOWN;
		break;
	}

	return pkg_type;
}


const struct amdgpu_smuio_funcs smuio_v13_0_3_funcs = {
	.get_die_id = smuio_v13_0_3_get_die_id,
	.get_socket_id = smuio_v13_0_3_get_socket_id,
	.get_pkg_type = smuio_v13_0_3_get_pkg_type,
};
