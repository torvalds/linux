/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
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
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
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
 *****************************************************************************/

#ifndef __fw_error_dump_h__
#define __fw_error_dump_h__

#include <linux/types.h>

#define IWL_FW_ERROR_DUMP_BARKER	0x14789632

/**
 * enum iwl_fw_error_dump_type - types of data in the dump file
 * @IWL_FW_ERROR_DUMP_SRAM:
 * @IWL_FW_ERROR_DUMP_REG:
 * @IWL_FW_ERROR_DUMP_RXF:
 * @IWL_FW_ERROR_DUMP_TXCMD: last TX command data, structured as
 *	&struct iwl_fw_error_dump_txcmd packets
 */
enum iwl_fw_error_dump_type {
	IWL_FW_ERROR_DUMP_SRAM = 0,
	IWL_FW_ERROR_DUMP_REG = 1,
	IWL_FW_ERROR_DUMP_RXF = 2,
	IWL_FW_ERROR_DUMP_TXCMD = 3,

	IWL_FW_ERROR_DUMP_MAX,
};

/**
 * struct iwl_fw_error_dump_data - data for one type
 * @type: %enum iwl_fw_error_dump_type
 * @len: the length starting from %data - must be a multiplier of 4.
 * @data: the data itself padded to be a multiplier of 4.
 */
struct iwl_fw_error_dump_data {
	__le32 type;
	__le32 len;
	__u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_file - the layout of the header of the file
 * @barker: must be %IWL_FW_ERROR_DUMP_BARKER
 * @file_len: the length of all the file starting from %barker
 * @data: array of %struct iwl_fw_error_dump_data
 */
struct iwl_fw_error_dump_file {
	__le32 barker;
	__le32 file_len;
	u8 data[0];
} __packed;

/**
 * struct iwl_fw_error_dump_txcmd - TX command data
 * @cmdlen: original length of command
 * @caplen: captured length of command (may be less)
 * @data: captured command data, @caplen bytes
 */
struct iwl_fw_error_dump_txcmd {
	__le32 cmdlen;
	__le32 caplen;
	u8 data[];
} __packed;

/**
 * iwl_mvm_fw_error_next_data - advance fw error dump data pointer
 * @data: previous data block
 * Returns: next data block
 */
static inline struct iwl_fw_error_dump_data *
iwl_mvm_fw_error_next_data(struct iwl_fw_error_dump_data *data)
{
	return (void *)(data->data + le32_to_cpu(data->len));
}

#endif /* __fw_error_dump_h__ */
