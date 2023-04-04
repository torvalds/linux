// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
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

#include <mali_kbase.h>
#include <csf/mali_kbase_csf_registers.h>
#include <gpu/mali_kbase_gpu_fault.h>

const char *kbase_gpu_exception_name(u32 const exception_code)
{
	const char *e;

	switch (exception_code) {
	/* CS exceptions */
	case CS_FAULT_EXCEPTION_TYPE_CS_RESOURCE_TERMINATED:
		e = "CS_RESOURCE_TERMINATED";
		break;
	case CS_FAULT_EXCEPTION_TYPE_CS_INHERIT_FAULT:
		e = "CS_INHERIT_FAULT";
		break;
	/* CS fatal exceptions */
	case CS_FATAL_EXCEPTION_TYPE_CS_CONFIG_FAULT:
		e = "CS_CONFIG_FAULT";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_ENDPOINT_FAULT:
		e = "FATAL_CS_ENDPOINT_FAULT";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_INVALID_INSTRUCTION:
		e = "FATAL_CS_INVALID_INSTRUCTION";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_CALL_STACK_OVERFLOW:
		e = "FATAL_CS_CALL_STACK_OVERFLOW";
		break;
	/*
	 * CS_FAULT_EXCEPTION_TYPE_CS_BUS_FAULT and CS_FATAL_EXCEPTION_TYPE_CS_BUS_FAULT share the same error code
	 * Type of CS_BUS_FAULT will be differentiated by CSF exception handler
	 */
	case CS_FAULT_EXCEPTION_TYPE_CS_BUS_FAULT:
		e = "CS_BUS_FAULT";
		break;
	/* Shader exceptions */
	case CS_FAULT_EXCEPTION_TYPE_INSTR_INVALID_PC:
		e = "INSTR_INVALID_PC";
		break;
	case CS_FAULT_EXCEPTION_TYPE_INSTR_INVALID_ENC:
		e = "INSTR_INVALID_ENC";
		break;
	case CS_FAULT_EXCEPTION_TYPE_INSTR_BARRIER_FAULT:
		e = "INSTR_BARRIER_FAULT";
		break;
	/* Iterator exceptions */
	case CS_FAULT_EXCEPTION_TYPE_KABOOM:
		e = "KABOOM";
		break;
	/* Misc exceptions */
	case CS_FAULT_EXCEPTION_TYPE_DATA_INVALID_FAULT:
		e = "DATA_INVALID_FAULT";
		break;
	case CS_FAULT_EXCEPTION_TYPE_TILE_RANGE_FAULT:
		e = "TILE_RANGE_FAULT";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ADDR_RANGE_FAULT:
		e = "ADDR_RANGE_FAULT";
		break;
	case CS_FAULT_EXCEPTION_TYPE_IMPRECISE_FAULT:
		e = "IMPRECISE_FAULT";
		break;
	/* FW exceptions */
	case CS_FATAL_EXCEPTION_TYPE_FIRMWARE_INTERNAL_ERROR:
		e = "FIRMWARE_INTERNAL_ERROR";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_UNRECOVERABLE:
		e = "CS_UNRECOVERABLE";
		break;
	case CS_FAULT_EXCEPTION_TYPE_RESOURCE_EVICTION_TIMEOUT:
		e = "RESOURCE_EVICTION_TIMEOUT";
		break;
	/* GPU Fault */
	case GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_BUS_FAULT:
		e = "GPU_BUS_FAULT";
		break;
	case GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_SHAREABILITY_FAULT:
		e = "GPU_SHAREABILITY_FAULT";
		break;
	case GPU_FAULTSTATUS_EXCEPTION_TYPE_SYSTEM_SHAREABILITY_FAULT:
		e = "SYSTEM_SHAREABILITY_FAULT";
		break;
	case GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_CACHEABILITY_FAULT:
		e = "GPU_CACHEABILITY_FAULT";
		break;
	/* MMU Fault */
	case CS_FAULT_EXCEPTION_TYPE_TRANSLATION_FAULT_L0:
		e = "TRANSLATION_FAULT at level 0";
		break;
	case CS_FAULT_EXCEPTION_TYPE_TRANSLATION_FAULT_L1:
		e = "TRANSLATION_FAULT at level 1";
		break;
	case CS_FAULT_EXCEPTION_TYPE_TRANSLATION_FAULT_L2:
		e = "TRANSLATION_FAULT at level 2";
		break;
	case CS_FAULT_EXCEPTION_TYPE_TRANSLATION_FAULT_L3:
		e = "TRANSLATION_FAULT at level 3";
		break;
	case CS_FAULT_EXCEPTION_TYPE_TRANSLATION_FAULT_L4:
		e = "TRANSLATION_FAULT";
		break;
	case CS_FAULT_EXCEPTION_TYPE_PERMISSION_FAULT_0:
		e = "PERMISSION_FAULT at level 0";
		break;
	case CS_FAULT_EXCEPTION_TYPE_PERMISSION_FAULT_1:
		e = "PERMISSION_FAULT at level 1";
		break;
	case CS_FAULT_EXCEPTION_TYPE_PERMISSION_FAULT_2:
		e = "PERMISSION_FAULT at level 2";
		break;
	case CS_FAULT_EXCEPTION_TYPE_PERMISSION_FAULT_3:
		e = "PERMISSION_FAULT at level 3";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ACCESS_FLAG_1:
		e = "ACCESS_FLAG at level 1";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ACCESS_FLAG_2:
		e = "ACCESS_FLAG at level 2";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ACCESS_FLAG_3:
		e = "ACCESS_FLAG at level 3";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_IN:
		e = "ADDRESS_SIZE_FAULT_IN";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT_0:
		e = "ADDRESS_SIZE_FAULT_OUT_0 at level 0";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT_1:
		e = "ADDRESS_SIZE_FAULT_OUT_1 at level 1";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT_2:
		e = "ADDRESS_SIZE_FAULT_OUT_2 at level 2";
		break;
	case CS_FAULT_EXCEPTION_TYPE_ADDRESS_SIZE_FAULT_OUT_3:
		e = "ADDRESS_SIZE_FAULT_OUT_3 at level 3";
		break;
	case CS_FAULT_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_0:
		e = "MEMORY_ATTRIBUTE_FAULT_0 at level 0";
		break;
	case CS_FAULT_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_1:
		e = "MEMORY_ATTRIBUTE_FAULT_1 at level 1";
		break;
	case CS_FAULT_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_2:
		e = "MEMORY_ATTRIBUTE_FAULT_2 at level 2";
		break;
	case CS_FAULT_EXCEPTION_TYPE_MEMORY_ATTRIBUTE_FAULT_3:
		e = "MEMORY_ATTRIBUTE_FAULT_3 at level 3";
		break;
	/* Any other exception code is unknown */
	default:
		e = "UNKNOWN";
		break;
	}

	return e;
}
