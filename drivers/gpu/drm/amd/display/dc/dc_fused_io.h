/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#ifndef __DC_FUSED_IO_H__
#define __DC_FUSED_IO_H__

#include "dc.h"
#include "mod_hdcp.h"

bool dm_atomic_write_poll_read_i2c(
		struct dc_link *link,
		const struct mod_hdcp_atomic_op_i2c *write,
		const struct mod_hdcp_atomic_op_i2c *poll,
		struct mod_hdcp_atomic_op_i2c *read,
		uint32_t poll_timeout_us,
		uint8_t poll_mask_msb
);

bool dm_atomic_write_poll_read_aux(
		struct dc_link *link,
		const struct mod_hdcp_atomic_op_aux *write,
		const struct mod_hdcp_atomic_op_aux *poll,
		struct mod_hdcp_atomic_op_aux *read,
		uint32_t poll_timeout_us,
		uint8_t poll_mask_msb
);

#endif  // __DC_FUSED_IO_H__

