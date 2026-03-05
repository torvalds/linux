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

#ifndef _DMUB_HW_LOCK_MGR_H_
#define _DMUB_HW_LOCK_MGR_H_

#include "dc_dmub_srv.h"
#include "core_types.h"

void dmub_hw_lock_mgr_cmd(struct dc_dmub_srv *dmub_srv,
				bool lock,
				union dmub_hw_lock_flags *hw_locks,
				struct dmub_hw_lock_inst_flags *inst_flags);

void dmub_hw_lock_mgr_inbox0_cmd(struct dc_dmub_srv *dmub_srv,
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd);

/**
 * should_use_dmub_inbox1_lock() - Checks if the DMCUB hardware lock via inbox1 should be used.
 *
 * @dc: pointer to DC object
 * @link: optional pointer to the link object to check for enabled link features
 *
 * Return: true if the inbox1 lock should be used, false otherwise
 */
bool should_use_dmub_inbox1_lock(const struct dc *dc, const struct dc_link *link);

/**
 * dmub_hw_lock_mgr_does_link_require_lock() - Returns true if the link has a feature that needs the HW lock.
 *
 * @dc: Pointer to DC object
 * @link: The link to check
 *
 * Return: true if the link has a feature that needs the HW lock, false otherwise
 */
bool dmub_hw_lock_mgr_does_link_require_lock(const struct dc *dc, const struct dc_link *link);

/**
 * dmub_hw_lock_mgr_does_context_require_lock() - Returns true if the context has any stream that needs the HW lock.
 *
 * @dc: Pointer to DC object
 * @context: The context to check
 *
 * Return: true if the context has any stream that needs the HW lock, false otherwise
 */
bool dmub_hw_lock_mgr_does_context_require_lock(const struct dc *dc, const struct dc_state *context);

/**
 * should_use_dmub_inbox0_lock_for_link() - Checks if the inbox0 interlock with DMU should be used.
 *
 * Is not functionally equivalent to inbox1 as DMUB will not own programming of the relevant locking
 * registers.
 *
 * @dc: pointer to DC object
 * @link: optional pointer to the link object to check for enabled link features
 *
 * Return: true if the inbox0 lock should be used, false otherwise
 */
bool should_use_dmub_inbox0_lock_for_link(const struct dc *dc, const struct dc_link *link);

#endif /*_DMUB_HW_LOCK_MGR_H_ */
