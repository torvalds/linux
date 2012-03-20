/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2010 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2010 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef __IWL_TESTMODE_H__
#define __IWL_TESTMODE_H__

#include <linux/types.h>


/*
 * Commands from user space to kernel space(IWL_TM_CMD_ID_APP2DEV_XX) and
 * from and kernel space to user space(IWL_TM_CMD_ID_DEV2APP_XX).
 * The command ID is carried with IWL_TM_ATTR_COMMAND.
 *
 * @IWL_TM_CMD_APP2DEV_UCODE:
 *	commands from user application to the uCode,
 *	the actual uCode host command ID is carried with
 *	IWL_TM_ATTR_UCODE_CMD_ID
 *
 * @IWL_TM_CMD_APP2DEV_DIRECT_REG_READ32:
 * @IWL_TM_CMD_APP2DEV_DIRECT_REG_WRITE32:
 * @IWL_TM_CMD_APP2DEV_DIRECT_REG_WRITE8:
 *	commands from user applicaiton to access register
 *
 * @IWL_TM_CMD_APP2DEV_GET_DEVICENAME: retrieve device name
 * @IWL_TM_CMD_APP2DEV_LOAD_INIT_FW: load initial uCode image
 * @IWL_TM_CMD_APP2DEV_CFG_INIT_CALIB: perform calibration
 * @IWL_TM_CMD_APP2DEV_LOAD_RUNTIME_FW: load runtime uCode image
 * @IWL_TM_CMD_APP2DEV_GET_EEPROM: request EEPROM data
 * @IWL_TM_CMD_APP2DEV_FIXRATE_REQ: set fix MCS
 *	commands fom user space for pure driver level operations
 *
 * @IWL_TM_CMD_APP2DEV_BEGIN_TRACE:
 * @IWL_TM_CMD_APP2DEV_END_TRACE:
 * @IWL_TM_CMD_APP2DEV_READ_TRACE:
 *	commands fom user space for uCode trace operations
 *
 * @IWL_TM_CMD_DEV2APP_SYNC_RSP:
 *	commands from kernel space to carry the synchronous response
 *	to user application
 * @IWL_TM_CMD_DEV2APP_UCODE_RX_PKT:
 *	commands from kernel space to multicast the spontaneous messages
 *	to user application
 * @IWL_TM_CMD_DEV2APP_EEPROM_RSP:
 *	commands from kernel space to carry the eeprom response
 *	to user application
 *
 * @IWL_TM_CMD_APP2DEV_OWNERSHIP:
 *	commands from user application to own change the ownership of the uCode
 *	if application has the ownership, the only host command from
 *	testmode will deliver to uCode. Default owner is driver
 *
 * @IWL_TM_CMD_APP2DEV_INDIRECT_REG_READ32:
 * @IWL_TM_CMD_APP2DEV_INDIRECT_REG_WRITE32:
 *	commands from user applicaiton to indirectly access peripheral register
 *
 * @IWL_TM_CMD_APP2DEV_READ_SRAM:
 * @IWL_TM_CMD_APP2DEV_DUMP_SRAM:
 *	commands from user applicaiton to read data in sram
 *
 * @IWL_TM_CMD_APP2DEV_LOAD_WOWLAN_FW: load Weak On Wireless LAN uCode image
 * @IWL_TM_CMD_APP2DEV_GET_FW_VERSION: retrieve uCode version
 * @IWL_TM_CMD_APP2DEV_GET_DEVICE_ID: retrieve ID information in device
 *
 */
enum iwl_tm_cmd_t {
	IWL_TM_CMD_APP2DEV_UCODE		= 1,
	IWL_TM_CMD_APP2DEV_DIRECT_REG_READ32	= 2,
	IWL_TM_CMD_APP2DEV_DIRECT_REG_WRITE32	= 3,
	IWL_TM_CMD_APP2DEV_DIRECT_REG_WRITE8	= 4,
	IWL_TM_CMD_APP2DEV_GET_DEVICENAME	= 5,
	IWL_TM_CMD_APP2DEV_LOAD_INIT_FW		= 6,
	IWL_TM_CMD_APP2DEV_CFG_INIT_CALIB	= 7,
	IWL_TM_CMD_APP2DEV_LOAD_RUNTIME_FW	= 8,
	IWL_TM_CMD_APP2DEV_GET_EEPROM		= 9,
	IWL_TM_CMD_APP2DEV_FIXRATE_REQ		= 10,
	IWL_TM_CMD_APP2DEV_BEGIN_TRACE		= 11,
	IWL_TM_CMD_APP2DEV_END_TRACE		= 12,
	IWL_TM_CMD_APP2DEV_READ_TRACE		= 13,
	IWL_TM_CMD_DEV2APP_SYNC_RSP		= 14,
	IWL_TM_CMD_DEV2APP_UCODE_RX_PKT		= 15,
	IWL_TM_CMD_DEV2APP_EEPROM_RSP		= 16,
	IWL_TM_CMD_APP2DEV_OWNERSHIP		= 17,
	IWL_TM_CMD_APP2DEV_INDIRECT_REG_READ32	= 18,
	IWL_TM_CMD_APP2DEV_INDIRECT_REG_WRITE32	= 19,
	IWL_TM_CMD_APP2DEV_READ_SRAM		= 20,
	IWL_TM_CMD_APP2DEV_DUMP_SRAM		= 21,
	IWL_TM_CMD_APP2DEV_LOAD_WOWLAN_FW	= 22,
	IWL_TM_CMD_APP2DEV_GET_FW_VERSION	= 23,
	IWL_TM_CMD_APP2DEV_GET_DEVICE_ID	= 24,
	IWL_TM_CMD_MAX				= 25,
};

/*
 * Atrribute filed in testmode command
 * See enum iwl_tm_cmd_t.
 *
 * @IWL_TM_ATTR_NOT_APPLICABLE:
 *	The attribute is not applicable or invalid
 * @IWL_TM_ATTR_COMMAND:
 *	From user space to kernel space:
 *	the command either destines to ucode, driver, or register;
 *	From kernel space to user space:
 *	the command either carries synchronous response,
 *	or the spontaneous message multicast from the device;
 *
 * @IWL_TM_ATTR_UCODE_CMD_ID:
 * @IWL_TM_ATTR_UCODE_CMD_DATA:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_UCODE,
 *	The mandatory fields are :
 *	IWL_TM_ATTR_UCODE_CMD_ID for recognizable command ID;
 *	IWL_TM_ATTR_COMMAND_FLAG for the flags of the commands;
 *	The optional fields are:
 *	IWL_TM_ATTR_UCODE_CMD_DATA for the actual command payload
 *	to the ucode
 *
 * @IWL_TM_ATTR_REG_OFFSET:
 * @IWL_TM_ATTR_REG_VALUE8:
 * @IWL_TM_ATTR_REG_VALUE32:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_REG_XXX,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_REG_OFFSET for the offset of the target register;
 *	IWL_TM_ATTR_REG_VALUE8 or IWL_TM_ATTR_REG_VALUE32 for value
 *
 * @IWL_TM_ATTR_SYNC_RSP:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_DEV2APP_SYNC_RSP,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_SYNC_RSP for the data content responding to the user
 *	application command
 *
 * @IWL_TM_ATTR_UCODE_RX_PKT:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_DEV2APP_UCODE_RX_PKT,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_UCODE_RX_PKT for the data content multicast to the user
 *	application
 *
 * @IWL_TM_ATTR_EEPROM:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_DEV2APP_EEPROM,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_EEPROM for the data content responging to the user
 *	application
 *
 * @IWL_TM_ATTR_TRACE_ADDR:
 * @IWL_TM_ATTR_TRACE_SIZE:
 * @IWL_TM_ATTR_TRACE_DUMP:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_XXX_TRACE,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_MEM_TRACE_ADDR for the trace address
 *	IWL_TM_ATTR_MEM_TRACE_SIZE for the trace buffer size
 *	IWL_TM_ATTR_MEM_TRACE_DUMP for the trace dump
 *
 * @IWL_TM_ATTR_FIXRATE:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_FIXRATE_REQ,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_FIXRATE for the fixed rate
 *
 * @IWL_TM_ATTR_UCODE_OWNER:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_OWNERSHIP,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_UCODE_OWNER for the new owner
 *
 * @IWL_TM_ATTR_SRAM_ADDR:
 * @IWL_TM_ATTR_SRAM_SIZE:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_READ_SRAM,
 *	The mandatory fields are:
 *	IWL_TM_ATTR_SRAM_ADDR for the address in sram
 *	IWL_TM_ATTR_SRAM_SIZE for the buffer size of data reading
 *
 * @IWL_TM_ATTR_SRAM_DUMP:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_DUMP_SRAM,
 *	IWL_TM_ATTR_SRAM_DUMP for the data in sram
 *
 * @IWL_TM_ATTR_FW_VERSION:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_GET_FW_VERSION,
 *	IWL_TM_ATTR_FW_VERSION for the uCode version
 *
 * @IWL_TM_ATTR_DEVICE_ID:
 *	When IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_GET_DEVICE_ID,
 *	IWL_TM_ATTR_DEVICE_ID for the device ID information
 *
 */
enum iwl_tm_attr_t {
	IWL_TM_ATTR_NOT_APPLICABLE		= 0,
	IWL_TM_ATTR_COMMAND			= 1,
	IWL_TM_ATTR_UCODE_CMD_ID		= 2,
	IWL_TM_ATTR_UCODE_CMD_DATA		= 3,
	IWL_TM_ATTR_REG_OFFSET			= 4,
	IWL_TM_ATTR_REG_VALUE8			= 5,
	IWL_TM_ATTR_REG_VALUE32			= 6,
	IWL_TM_ATTR_SYNC_RSP			= 7,
	IWL_TM_ATTR_UCODE_RX_PKT		= 8,
	IWL_TM_ATTR_EEPROM			= 9,
	IWL_TM_ATTR_TRACE_ADDR			= 10,
	IWL_TM_ATTR_TRACE_SIZE			= 11,
	IWL_TM_ATTR_TRACE_DUMP			= 12,
	IWL_TM_ATTR_FIXRATE			= 13,
	IWL_TM_ATTR_UCODE_OWNER			= 14,
	IWL_TM_ATTR_SRAM_ADDR			= 15,
	IWL_TM_ATTR_SRAM_SIZE			= 16,
	IWL_TM_ATTR_SRAM_DUMP			= 17,
	IWL_TM_ATTR_FW_VERSION			= 18,
	IWL_TM_ATTR_DEVICE_ID			= 19,
	IWL_TM_ATTR_MAX				= 20,
};

/* uCode trace buffer */
#define TRACE_BUFF_SIZE_MAX	0x200000
#define TRACE_BUFF_SIZE_MIN	0x20000
#define TRACE_BUFF_SIZE_DEF	TRACE_BUFF_SIZE_MIN
#define TRACE_BUFF_PADD		0x2000

/* Maximum data size of each dump it packet */
#define DUMP_CHUNK_SIZE		(PAGE_SIZE - 1024)

#endif
