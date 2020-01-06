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

#ifndef _DMUB_FW_STATE_H_
#define _DMUB_FW_STATE_H_

#include "dmub_types.h"

#pragma pack(push, 1)

struct dmub_fw_state {
	/**
	 * @phy_initialized_during_fw_boot:
	 *
	 * Detects if VBIOS/VBL has ran before firmware boot.
	 * A value of 1 will usually mean S0i3 boot.
	 */
	uint8_t phy_initialized_during_fw_boot;

	/**
	 * @intialized_phy:
	 *
	 * Bit vector of initialized PHY.
	 */
	uint8_t initialized_phy;

	/**
	 * @enabled_phy:
	 *
	 * Bit vector of enabled PHY for DP alt mode switch tracking.
	 */
	uint8_t enabled_phy;

	/**
	 * @dmcu_fw_loaded:
	 *
	 * DMCU auto load state.
	 */
	uint8_t dmcu_fw_loaded;

	/**
	 * @psr_state:
	 *
	 * PSR state tracking.
	 */
	uint8_t psr_state;
};

#pragma pack(pop)

#endif /* _DMUB_FW_STATE_H_ */
