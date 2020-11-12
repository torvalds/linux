/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include "csf/mali_gpu_csf_registers.h"
#include "../mali_kbase_gpu_fault.h"

const char *kbase_gpu_exception_name(u32 const exception_code)
{
	const char *e;

	switch (exception_code) {
	/* Command Stream exceptions */
	case CS_FAULT_EXCEPTION_TYPE_CS_RESOURCE_TERMINATED:
		e = "CS_RESOURCE_TERMINATED";
		break;
	case CS_FAULT_EXCEPTION_TYPE_CS_INHERIT_FAULT:
		e = "CS_INHERIT_FAULT";
		break;
	/* Command Stream fatal exceptions */
	case CS_FATAL_EXCEPTION_TYPE_CS_CONFIG_FAULT:
		e = "CS_CONFIG_FAULT";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_ENDPOINT_FAULT:
		e = "FATAL_CS_ENDPOINT_FAULT";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_BUS_FAULT:
		e = "FATAL_CS_BUS_FAULT";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_INVALID_INSTRUCTION:
		e = "FATAL_CS_INVALID_INSTRUCTION";
		break;
	case CS_FATAL_EXCEPTION_TYPE_CS_CALL_STACK_OVERFLOW:
		e = "FATAL_CS_CALL_STACK_OVERFLOW";
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
	/* Any other exception code is unknown */
	default:
		e = "UNKNOWN";
		break;
	}

	return e;
}
