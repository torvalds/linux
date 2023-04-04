/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2023 ARM Limited. All rights reserved.
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

#ifndef _KBASE_GPU_FAULT_H_
#define _KBASE_GPU_FAULT_H_

/**
 * kbase_gpu_exception_name() - Returns associated string of the exception code
 *
 * @exception_code: exception code
 *
 * This function is called by error handlers when GPU reports an error.
 *
 * Return: Error string associated with the exception code
 */
const char *kbase_gpu_exception_name(u32 exception_code);

/**
 * kbase_gpu_access_type_name - Convert MMU_AS_CONTROL.FAULTSTATUS.ACCESS_TYPE
 * into string.
 * @fault_status:  value of FAULTSTATUS register.
 *
 * After MMU fault, this function can be used to get readable information about
 * access_type of the MMU fault.
 *
 * Return: String of the access type.
 */
const char *kbase_gpu_access_type_name(u32 fault_status);

#endif /* _KBASE_GPU_FAULT_H_ */
