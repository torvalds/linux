/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
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
 * Backend-specific Power Manager MCU state definitions.
 * The function-like macro KBASEP_MCU_STATE() must be defined before including
 * this header file. This header file can be included multiple times in the
 * same compilation unit with different definitions of KBASEP_MCU_STATE().
 *
 * @OFF:                      The MCU is powered off.
 * @PEND_ON_RELOAD:           The warm boot of MCU or cold boot of MCU (with
 *                            firmware reloading) is in progress.
 * @ON_GLB_REINIT_PEND:       The MCU is enabled and Global configuration
 *                            requests have been sent to the firmware.
 * @ON_HWCNT_ENABLE:          The Global requests have completed and MCU is now
 *                            ready for use and hwcnt is being enabled.
 * @ON:                       The MCU is active and hwcnt has been enabled.
 * @ON_CORE_ATTR_UPDATE_PEND: The MCU is active and mask of enabled shader cores
 *                            is being updated.
 * @ON_HWCNT_DISABLE:         The MCU is on and hwcnt is being disabled.
 * @ON_HALT:                  The MCU is on and hwcnt has been disabled, MCU
 *                            halt would be triggered.
 * @ON_PEND_HALT:             MCU halt in progress, confirmation pending.
 * @POWER_DOWN:               MCU halted operations, pending being disabled.
 * @PEND_OFF:                 MCU is being disabled, pending on powering off.
 * @RESET_WAIT:               The GPU is resetting, MCU state is unknown.
 */
KBASEP_MCU_STATE(OFF)
KBASEP_MCU_STATE(PEND_ON_RELOAD)
KBASEP_MCU_STATE(ON_GLB_REINIT_PEND)
KBASEP_MCU_STATE(ON_HWCNT_ENABLE)
KBASEP_MCU_STATE(ON)
KBASEP_MCU_STATE(ON_CORE_ATTR_UPDATE_PEND)
KBASEP_MCU_STATE(ON_HWCNT_DISABLE)
KBASEP_MCU_STATE(ON_HALT)
KBASEP_MCU_STATE(ON_PEND_HALT)
KBASEP_MCU_STATE(POWER_DOWN)
KBASEP_MCU_STATE(PEND_OFF)
KBASEP_MCU_STATE(RESET_WAIT)
/* Additional MCU states with HOST_CONTROL_SHADERS */
KBASEP_MCU_STATE(HCTL_SHADERS_PEND_ON)
KBASEP_MCU_STATE(HCTL_CORES_NOTIFY_PEND)
KBASEP_MCU_STATE(HCTL_MCU_ON_RECHECK)
KBASEP_MCU_STATE(HCTL_SHADERS_READY_OFF)
KBASEP_MCU_STATE(HCTL_SHADERS_PEND_OFF)
