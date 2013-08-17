/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file osk/src/common/mali_osk_compile_asserts.h
 *
 * Private definitions of compile time asserts.
 **/

/**
 * Unreachable function needed to check values at compile-time, in both debug
 * and release builds
 */
void oskp_cmn_compile_time_assertions(void);

