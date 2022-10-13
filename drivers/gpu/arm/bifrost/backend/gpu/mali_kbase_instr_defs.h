/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2016, 2018-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Backend-specific instrumentation definitions
 */

#ifndef _KBASE_INSTR_DEFS_H_
#define _KBASE_INSTR_DEFS_H_

#include <hwcnt/mali_kbase_hwcnt_gpu.h>

/*
 * Instrumentation State Machine States
 */
enum kbase_instr_state {
	/* State where instrumentation is not active */
	KBASE_INSTR_STATE_DISABLED = 0,
	/* State machine is active and ready for a command. */
	KBASE_INSTR_STATE_IDLE,
	/* Hardware is currently dumping a frame. */
	KBASE_INSTR_STATE_DUMPING,
	/* An error has occurred during DUMPING (page fault). */
	KBASE_INSTR_STATE_FAULT,
	/* An unrecoverable error has occurred, a reset is the only way to exit
	 * from unrecoverable error state.
	 */
	KBASE_INSTR_STATE_UNRECOVERABLE_ERROR,
};

/* Structure used for instrumentation and HW counters dumping */
struct kbase_instr_backend {
	wait_queue_head_t wait;
	int triggered;
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
	enum kbase_hwcnt_physical_set override_counter_set;
#endif

	enum kbase_instr_state state;
};

#endif /* _KBASE_INSTR_DEFS_H_ */
