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
 * Backend-specific Power Manager level 2 cache state definitions.
 * The function-like macro KBASEP_L2_STATE() must be defined before including
 * this header file. This header file can be included multiple times in the
 * same compilation unit with different definitions of KBASEP_L2_STATE().
 *
 * @OFF:              The L2 cache and tiler are off
 * @PEND_ON:          The L2 cache and tiler are powering on
 * @RESTORE_CLOCKS:   The GPU clock is restored. Conditionally used.
 * @ON_HWCNT_ENABLE:  The L2 cache and tiler are on, and hwcnt is being enabled
 * @ON:               The L2 cache and tiler are on, and hwcnt is enabled
 * @ON_HWCNT_DISABLE: The L2 cache and tiler are on, and hwcnt is being disabled
 * @SLOW_DOWN_CLOCKS: The GPU clock is set to appropriate or lowest clock.
 *                    Conditionally used.
 * @POWER_DOWN:       The L2 cache and tiler are about to be powered off
 * @PEND_OFF:         The L2 cache and tiler are powering off
 * @RESET_WAIT:       The GPU is resetting, L2 cache and tiler power state are
 *                    unknown
 */
KBASEP_L2_STATE(OFF)
KBASEP_L2_STATE(PEND_ON)
KBASEP_L2_STATE(RESTORE_CLOCKS)
KBASEP_L2_STATE(ON_HWCNT_ENABLE)
KBASEP_L2_STATE(ON)
KBASEP_L2_STATE(ON_HWCNT_DISABLE)
KBASEP_L2_STATE(SLOW_DOWN_CLOCKS)
KBASEP_L2_STATE(POWER_DOWN)
KBASEP_L2_STATE(PEND_OFF)
KBASEP_L2_STATE(RESET_WAIT)
