/*
 *
 * (C) COPYRIGHT 2014, 2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/*
 * Backend-specific instrumentation definitions
 */

#ifndef _KBASE_INSTR_DEFS_H_
#define _KBASE_INSTR_DEFS_H_

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
	/* We've requested a clean to occur on a workqueue */
	KBASE_INSTR_STATE_REQUEST_CLEAN,
	/* Hardware is currently cleaning and invalidating caches. */
	KBASE_INSTR_STATE_CLEANING,
	/* Cache clean completed, and either a) a dump is complete, or
	 * b) instrumentation can now be setup. */
	KBASE_INSTR_STATE_CLEANED,
	/* An error has occured during DUMPING (page fault). */
	KBASE_INSTR_STATE_FAULT
};

/* Structure used for instrumentation and HW counters dumping */
struct kbase_instr_backend {
	wait_queue_head_t wait;
	int triggered;

	enum kbase_instr_state state;
	wait_queue_head_t cache_clean_wait;
	struct workqueue_struct *cache_clean_wq;
	struct work_struct  cache_clean_work;
};

#endif /* _KBASE_INSTR_DEFS_H_ */

