/*
 *
 * (C) COPYRIGHT 2018-2019 ARM Limited. All rights reserved.
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
 * Backend-specific Power Manager shader core state definitions.
 * The function-like macro KBASEP_SHADER_STATE() must be defined before
 * including this header file. This header file can be included multiple
 * times in the same compilation unit with different definitions of
 * KBASEP_SHADER_STATE().
 */
KBASEP_SHADER_STATE(OFF_CORESTACK_OFF)
KBASEP_SHADER_STATE(OFF_CORESTACK_PEND_ON)
KBASEP_SHADER_STATE(PEND_ON_CORESTACK_ON)
KBASEP_SHADER_STATE(ON_CORESTACK_ON)
KBASEP_SHADER_STATE(ON_CORESTACK_ON_RECHECK)
KBASEP_SHADER_STATE(WAIT_OFF_CORESTACK_ON)
#if !MALI_USE_CSF
KBASEP_SHADER_STATE(WAIT_GPU_IDLE)
#endif /* !MALI_USE_CSF */
KBASEP_SHADER_STATE(WAIT_FINISHED_CORESTACK_ON)
KBASEP_SHADER_STATE(L2_FLUSHING_CORESTACK_ON)
KBASEP_SHADER_STATE(READY_OFF_CORESTACK_ON)
KBASEP_SHADER_STATE(PEND_OFF_CORESTACK_ON)
KBASEP_SHADER_STATE(OFF_CORESTACK_PEND_OFF)
KBASEP_SHADER_STATE(OFF_CORESTACK_OFF_TIMER_PEND_OFF)
KBASEP_SHADER_STATE(RESET_WAIT)
