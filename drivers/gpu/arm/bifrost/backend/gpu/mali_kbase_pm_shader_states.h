/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * (C) COPYRIGHT 2018-2021 ARM Limited. All rights reserved.
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
 * Backend-specific Power Manager shader core state definitions.
 * The function-like macro KBASEP_SHADER_STATE() must be defined before
 * including this header file. This header file can be included multiple
 * times in the same compilation unit with different definitions of
 * KBASEP_SHADER_STATE().
 *
 * @OFF_CORESTACK_OFF:                The shaders and core stacks are off
 * @OFF_CORESTACK_PEND_ON:            The shaders are off, core stacks have been
 *                                    requested to power on and hwcnt is being
 *                                    disabled
 * @PEND_ON_CORESTACK_ON:             Core stacks are on, shaders have been
 *                                    requested to power on. Or after doing
 *                                    partial shader on/off, checking whether
 *                                    it's the desired state.
 * @ON_CORESTACK_ON:                  The shaders and core stacks are on, and
 *                                    hwcnt already enabled.
 * @ON_CORESTACK_ON_RECHECK:          The shaders and core stacks are on, hwcnt
 *                                    disabled, and checks to powering down or
 *                                    re-enabling hwcnt.
 * @WAIT_OFF_CORESTACK_ON:            The shaders have been requested to power
 *                                    off, but they remain on for the duration
 *                                    of the hysteresis timer
 * @WAIT_GPU_IDLE:                    The shaders partial poweroff needs to
 *                                    reach a state where jobs on the GPU are
 *                                    finished including jobs currently running
 *                                    and in the GPU queue because of
 *                                    GPU2017-861
 * @WAIT_FINISHED_CORESTACK_ON:       The hysteresis timer has expired
 * @L2_FLUSHING_CORESTACK_ON:         The core stacks are on and the level 2
 *                                    cache is being flushed.
 * @READY_OFF_CORESTACK_ON:           The core stacks are on and the shaders are
 *                                    ready to be powered off.
 * @PEND_OFF_CORESTACK_ON:            The core stacks are on, and the shaders
 *                                    have been requested to power off
 * @OFF_CORESTACK_PEND_OFF:           The shaders are off, and the core stacks
 *                                    have been requested to power off
 * @OFF_CORESTACK_OFF_TIMER_PEND_OFF: Shaders and corestacks are off, but the
 *                                    tick timer cancellation is still pending.
 * @RESET_WAIT:                       The GPU is resetting, shader and core
 *                                    stack power states are unknown
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
