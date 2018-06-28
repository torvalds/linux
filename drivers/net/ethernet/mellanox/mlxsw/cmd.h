/*
 * drivers/net/ethernet/mellanox/mlxsw/cmd.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_CMD_H
#define _MLXSW_CMD_H

#include "item.h"

#define MLXSW_CMD_MBOX_SIZE	4096

static inline char *mlxsw_cmd_mbox_alloc(void)
{
	return kzalloc(MLXSW_CMD_MBOX_SIZE, GFP_KERNEL);
}

static inline void mlxsw_cmd_mbox_free(char *mbox)
{
	kfree(mbox);
}

static inline void mlxsw_cmd_mbox_zero(char *mbox)
{
	memset(mbox, 0, MLXSW_CMD_MBOX_SIZE);
}

struct mlxsw_core;

int mlxsw_cmd_exec(struct mlxsw_core *mlxsw_core, u16 opcode, u8 opcode_mod,
		   u32 in_mod, bool out_mbox_direct, bool reset_ok,
		   char *in_mbox, size_t in_mbox_size,
		   char *out_mbox, size_t out_mbox_size);

static inline int mlxsw_cmd_exec_in(struct mlxsw_core *mlxsw_core, u16 opcode,
				    u8 opcode_mod, u32 in_mod, char *in_mbox,
				    size_t in_mbox_size)
{
	return mlxsw_cmd_exec(mlxsw_core, opcode, opcode_mod, in_mod, false,
			      false, in_mbox, in_mbox_size, NULL, 0);
}

static inline int mlxsw_cmd_exec_out(struct mlxsw_core *mlxsw_core, u16 opcode,
				     u8 opcode_mod, u32 in_mod,
				     bool out_mbox_direct,
				     char *out_mbox, size_t out_mbox_size)
{
	return mlxsw_cmd_exec(mlxsw_core, opcode, opcode_mod, in_mod,
			      out_mbox_direct, false, NULL, 0,
			      out_mbox, out_mbox_size);
}

static inline int mlxsw_cmd_exec_none(struct mlxsw_core *mlxsw_core, u16 opcode,
				      u8 opcode_mod, u32 in_mod)
{
	return mlxsw_cmd_exec(mlxsw_core, opcode, opcode_mod, in_mod, false,
			      false, NULL, 0, NULL, 0);
}

enum mlxsw_cmd_opcode {
	MLXSW_CMD_OPCODE_QUERY_FW		= 0x004,
	MLXSW_CMD_OPCODE_QUERY_BOARDINFO	= 0x006,
	MLXSW_CMD_OPCODE_QUERY_AQ_CAP		= 0x003,
	MLXSW_CMD_OPCODE_MAP_FA			= 0xFFF,
	MLXSW_CMD_OPCODE_UNMAP_FA		= 0xFFE,
	MLXSW_CMD_OPCODE_CONFIG_PROFILE		= 0x100,
	MLXSW_CMD_OPCODE_ACCESS_REG		= 0x040,
	MLXSW_CMD_OPCODE_SW2HW_DQ		= 0x201,
	MLXSW_CMD_OPCODE_HW2SW_DQ		= 0x202,
	MLXSW_CMD_OPCODE_2ERR_DQ		= 0x01E,
	MLXSW_CMD_OPCODE_QUERY_DQ		= 0x022,
	MLXSW_CMD_OPCODE_SW2HW_CQ		= 0x016,
	MLXSW_CMD_OPCODE_HW2SW_CQ		= 0x017,
	MLXSW_CMD_OPCODE_QUERY_CQ		= 0x018,
	MLXSW_CMD_OPCODE_SW2HW_EQ		= 0x013,
	MLXSW_CMD_OPCODE_HW2SW_EQ		= 0x014,
	MLXSW_CMD_OPCODE_QUERY_EQ		= 0x015,
	MLXSW_CMD_OPCODE_QUERY_RESOURCES	= 0x101,
};

static inline const char *mlxsw_cmd_opcode_str(u16 opcode)
{
	switch (opcode) {
	case MLXSW_CMD_OPCODE_QUERY_FW:
		return "QUERY_FW";
	case MLXSW_CMD_OPCODE_QUERY_BOARDINFO:
		return "QUERY_BOARDINFO";
	case MLXSW_CMD_OPCODE_QUERY_AQ_CAP:
		return "QUERY_AQ_CAP";
	case MLXSW_CMD_OPCODE_MAP_FA:
		return "MAP_FA";
	case MLXSW_CMD_OPCODE_UNMAP_FA:
		return "UNMAP_FA";
	case MLXSW_CMD_OPCODE_CONFIG_PROFILE:
		return "CONFIG_PROFILE";
	case MLXSW_CMD_OPCODE_ACCESS_REG:
		return "ACCESS_REG";
	case MLXSW_CMD_OPCODE_SW2HW_DQ:
		return "SW2HW_DQ";
	case MLXSW_CMD_OPCODE_HW2SW_DQ:
		return "HW2SW_DQ";
	case MLXSW_CMD_OPCODE_2ERR_DQ:
		return "2ERR_DQ";
	case MLXSW_CMD_OPCODE_QUERY_DQ:
		return "QUERY_DQ";
	case MLXSW_CMD_OPCODE_SW2HW_CQ:
		return "SW2HW_CQ";
	case MLXSW_CMD_OPCODE_HW2SW_CQ:
		return "HW2SW_CQ";
	case MLXSW_CMD_OPCODE_QUERY_CQ:
		return "QUERY_CQ";
	case MLXSW_CMD_OPCODE_SW2HW_EQ:
		return "SW2HW_EQ";
	case MLXSW_CMD_OPCODE_HW2SW_EQ:
		return "HW2SW_EQ";
	case MLXSW_CMD_OPCODE_QUERY_EQ:
		return "QUERY_EQ";
	case MLXSW_CMD_OPCODE_QUERY_RESOURCES:
		return "QUERY_RESOURCES";
	default:
		return "*UNKNOWN*";
	}
}

enum mlxsw_cmd_status {
	/* Command execution succeeded. */
	MLXSW_CMD_STATUS_OK		= 0x00,
	/* Internal error (e.g. bus error) occurred while processing command. */
	MLXSW_CMD_STATUS_INTERNAL_ERR	= 0x01,
	/* Operation/command not supported or opcode modifier not supported. */
	MLXSW_CMD_STATUS_BAD_OP		= 0x02,
	/* Parameter not supported, parameter out of range. */
	MLXSW_CMD_STATUS_BAD_PARAM	= 0x03,
	/* System was not enabled or bad system state. */
	MLXSW_CMD_STATUS_BAD_SYS_STATE	= 0x04,
	/* Attempt to access reserved or unallocated resource, or resource in
	 * inappropriate ownership.
	 */
	MLXSW_CMD_STATUS_BAD_RESOURCE	= 0x05,
	/* Requested resource is currently executing a command. */
	MLXSW_CMD_STATUS_RESOURCE_BUSY	= 0x06,
	/* Required capability exceeds device limits. */
	MLXSW_CMD_STATUS_EXCEED_LIM	= 0x08,
	/* Resource is not in the appropriate state or ownership. */
	MLXSW_CMD_STATUS_BAD_RES_STATE	= 0x09,
	/* Index out of range (might be beyond table size or attempt to
	 * access a reserved resource).
	 */
	MLXSW_CMD_STATUS_BAD_INDEX	= 0x0A,
	/* NVMEM checksum/CRC failed. */
	MLXSW_CMD_STATUS_BAD_NVMEM	= 0x0B,
	/* Device is currently running reset */
	MLXSW_CMD_STATUS_RUNNING_RESET	= 0x26,
	/* Bad management packet (silently discarded). */
	MLXSW_CMD_STATUS_BAD_PKT	= 0x30,
};

static inline const char *mlxsw_cmd_status_str(u8 status)
{
	switch (status) {
	case MLXSW_CMD_STATUS_OK:
		return "OK";
	case MLXSW_CMD_STATUS_INTERNAL_ERR:
		return "INTERNAL_ERR";
	case MLXSW_CMD_STATUS_BAD_OP:
		return "BAD_OP";
	case MLXSW_CMD_STATUS_BAD_PARAM:
		return "BAD_PARAM";
	case MLXSW_CMD_STATUS_BAD_SYS_STATE:
		return "BAD_SYS_STATE";
	case MLXSW_CMD_STATUS_BAD_RESOURCE:
		return "BAD_RESOURCE";
	case MLXSW_CMD_STATUS_RESOURCE_BUSY:
		return "RESOURCE_BUSY";
	case MLXSW_CMD_STATUS_EXCEED_LIM:
		return "EXCEED_LIM";
	case MLXSW_CMD_STATUS_BAD_RES_STATE:
		return "BAD_RES_STATE";
	case MLXSW_CMD_STATUS_BAD_INDEX:
		return "BAD_INDEX";
	case MLXSW_CMD_STATUS_BAD_NVMEM:
		return "BAD_NVMEM";
	case MLXSW_CMD_STATUS_RUNNING_RESET:
		return "RUNNING_RESET";
	case MLXSW_CMD_STATUS_BAD_PKT:
		return "BAD_PKT";
	default:
		return "*UNKNOWN*";
	}
}

/* QUERY_FW - Query Firmware
 * -------------------------
 * OpMod == 0, INMmod == 0
 * -----------------------
 * The QUERY_FW command retrieves information related to firmware, command
 * interface version and the amount of resources that should be allocated to
 * the firmware.
 */

static inline int mlxsw_cmd_query_fw(struct mlxsw_core *mlxsw_core,
				     char *out_mbox)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_QUERY_FW,
				  0, 0, false, out_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_query_fw_fw_pages
 * Amount of physical memory to be allocatedfor firmware usage in 4KB pages.
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_pages, 0x00, 16, 16);

/* cmd_mbox_query_fw_fw_rev_major
 * Firmware Revision - Major
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_rev_major, 0x00, 0, 16);

/* cmd_mbox_query_fw_fw_rev_subminor
 * Firmware Sub-minor version (Patch level)
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_rev_subminor, 0x04, 16, 16);

/* cmd_mbox_query_fw_fw_rev_minor
 * Firmware Revision - Minor
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_rev_minor, 0x04, 0, 16);

/* cmd_mbox_query_fw_core_clk
 * Internal Clock Frequency (in MHz)
 */
MLXSW_ITEM32(cmd_mbox, query_fw, core_clk, 0x08, 16, 16);

/* cmd_mbox_query_fw_cmd_interface_rev
 * Command Interface Interpreter Revision ID. This number is bumped up
 * every time a non-backward-compatible change is done for the command
 * interface. The current cmd_interface_rev is 1.
 */
MLXSW_ITEM32(cmd_mbox, query_fw, cmd_interface_rev, 0x08, 0, 16);

/* cmd_mbox_query_fw_dt
 * If set, Debug Trace is supported
 */
MLXSW_ITEM32(cmd_mbox, query_fw, dt, 0x0C, 31, 1);

/* cmd_mbox_query_fw_api_version
 * Indicates the version of the API, to enable software querying
 * for compatibility. The current api_version is 1.
 */
MLXSW_ITEM32(cmd_mbox, query_fw, api_version, 0x0C, 0, 16);

/* cmd_mbox_query_fw_fw_hour
 * Firmware timestamp - hour
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_hour, 0x10, 24, 8);

/* cmd_mbox_query_fw_fw_minutes
 * Firmware timestamp - minutes
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_minutes, 0x10, 16, 8);

/* cmd_mbox_query_fw_fw_seconds
 * Firmware timestamp - seconds
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_seconds, 0x10, 8, 8);

/* cmd_mbox_query_fw_fw_year
 * Firmware timestamp - year
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_year, 0x14, 16, 16);

/* cmd_mbox_query_fw_fw_month
 * Firmware timestamp - month
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_month, 0x14, 8, 8);

/* cmd_mbox_query_fw_fw_day
 * Firmware timestamp - day
 */
MLXSW_ITEM32(cmd_mbox, query_fw, fw_day, 0x14, 0, 8);

/* cmd_mbox_query_fw_clr_int_base_offset
 * Clear Interrupt register's offset from clr_int_bar register
 * in PCI address space.
 */
MLXSW_ITEM64(cmd_mbox, query_fw, clr_int_base_offset, 0x20, 0, 64);

/* cmd_mbox_query_fw_clr_int_bar
 * PCI base address register (BAR) where clr_int register is located.
 * 00 - BAR 0-1 (64 bit BAR)
 */
MLXSW_ITEM32(cmd_mbox, query_fw, clr_int_bar, 0x28, 30, 2);

/* cmd_mbox_query_fw_error_buf_offset
 * Read Only buffer for internal error reports of offset
 * from error_buf_bar register in PCI address space).
 */
MLXSW_ITEM64(cmd_mbox, query_fw, error_buf_offset, 0x30, 0, 64);

/* cmd_mbox_query_fw_error_buf_size
 * Internal error buffer size in DWORDs
 */
MLXSW_ITEM32(cmd_mbox, query_fw, error_buf_size, 0x38, 0, 32);

/* cmd_mbox_query_fw_error_int_bar
 * PCI base address register (BAR) where error buffer
 * register is located.
 * 00 - BAR 0-1 (64 bit BAR)
 */
MLXSW_ITEM32(cmd_mbox, query_fw, error_int_bar, 0x3C, 30, 2);

/* cmd_mbox_query_fw_doorbell_page_offset
 * Offset of the doorbell page
 */
MLXSW_ITEM64(cmd_mbox, query_fw, doorbell_page_offset, 0x40, 0, 64);

/* cmd_mbox_query_fw_doorbell_page_bar
 * PCI base address register (BAR) of the doorbell page
 * 00 - BAR 0-1 (64 bit BAR)
 */
MLXSW_ITEM32(cmd_mbox, query_fw, doorbell_page_bar, 0x48, 30, 2);

/* QUERY_BOARDINFO - Query Board Information
 * -----------------------------------------
 * OpMod == 0 (N/A), INMmod == 0 (N/A)
 * -----------------------------------
 * The QUERY_BOARDINFO command retrieves adapter specific parameters.
 */

static inline int mlxsw_cmd_boardinfo(struct mlxsw_core *mlxsw_core,
				      char *out_mbox)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_QUERY_BOARDINFO,
				  0, 0, false, out_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_boardinfo_intapin
 * When PCIe interrupt messages are being used, this value is used for clearing
 * an interrupt. When using MSI-X, this register is not used.
 */
MLXSW_ITEM32(cmd_mbox, boardinfo, intapin, 0x10, 24, 8);

/* cmd_mbox_boardinfo_vsd_vendor_id
 * PCISIG Vendor ID (www.pcisig.com/membership/vid_search) of the vendor
 * specifying/formatting the VSD. The vsd_vendor_id identifies the management
 * domain of the VSD/PSID data. Different vendors may choose different VSD/PSID
 * format and encoding as long as they use their assigned vsd_vendor_id.
 */
MLXSW_ITEM32(cmd_mbox, boardinfo, vsd_vendor_id, 0x1C, 0, 16);

/* cmd_mbox_boardinfo_vsd
 * Vendor Specific Data. The VSD string that is burnt to the Flash
 * with the firmware.
 */
#define MLXSW_CMD_BOARDINFO_VSD_LEN 208
MLXSW_ITEM_BUF(cmd_mbox, boardinfo, vsd, 0x20, MLXSW_CMD_BOARDINFO_VSD_LEN);

/* cmd_mbox_boardinfo_psid
 * The PSID field is a 16-ascii (byte) character string which acts as
 * the board ID. The PSID format is used in conjunction with
 * Mellanox vsd_vendor_id (15B3h).
 */
#define MLXSW_CMD_BOARDINFO_PSID_LEN 16
MLXSW_ITEM_BUF(cmd_mbox, boardinfo, psid, 0xF0, MLXSW_CMD_BOARDINFO_PSID_LEN);

/* QUERY_AQ_CAP - Query Asynchronous Queues Capabilities
 * -----------------------------------------------------
 * OpMod == 0 (N/A), INMmod == 0 (N/A)
 * -----------------------------------
 * The QUERY_AQ_CAP command returns the device asynchronous queues
 * capabilities supported.
 */

static inline int mlxsw_cmd_query_aq_cap(struct mlxsw_core *mlxsw_core,
					 char *out_mbox)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_QUERY_AQ_CAP,
				  0, 0, false, out_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_query_aq_cap_log_max_sdq_sz
 * Log (base 2) of max WQEs allowed on SDQ.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, log_max_sdq_sz, 0x00, 24, 8);

/* cmd_mbox_query_aq_cap_max_num_sdqs
 * Maximum number of SDQs.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, max_num_sdqs, 0x00, 0, 8);

/* cmd_mbox_query_aq_cap_log_max_rdq_sz
 * Log (base 2) of max WQEs allowed on RDQ.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, log_max_rdq_sz, 0x04, 24, 8);

/* cmd_mbox_query_aq_cap_max_num_rdqs
 * Maximum number of RDQs.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, max_num_rdqs, 0x04, 0, 8);

/* cmd_mbox_query_aq_cap_log_max_cq_sz
 * Log (base 2) of the Maximum CQEs allowed in a CQ for CQEv0 and CQEv1.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, log_max_cq_sz, 0x08, 24, 8);

/* cmd_mbox_query_aq_cap_log_max_cqv2_sz
 * Log (base 2) of the Maximum CQEs allowed in a CQ for CQEv2.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, log_max_cqv2_sz, 0x08, 16, 8);

/* cmd_mbox_query_aq_cap_max_num_cqs
 * Maximum number of CQs.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, max_num_cqs, 0x08, 0, 8);

/* cmd_mbox_query_aq_cap_log_max_eq_sz
 * Log (base 2) of max EQEs allowed on EQ.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, log_max_eq_sz, 0x0C, 24, 8);

/* cmd_mbox_query_aq_cap_max_num_eqs
 * Maximum number of EQs.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, max_num_eqs, 0x0C, 0, 8);

/* cmd_mbox_query_aq_cap_max_sg_sq
 * The maximum S/G list elements in an DSQ. DSQ must not contain
 * more S/G entries than indicated here.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, max_sg_sq, 0x10, 8, 8);

/* cmd_mbox_query_aq_cap_
 * The maximum S/G list elements in an DRQ. DRQ must not contain
 * more S/G entries than indicated here.
 */
MLXSW_ITEM32(cmd_mbox, query_aq_cap, max_sg_rq, 0x10, 0, 8);

/* MAP_FA - Map Firmware Area
 * --------------------------
 * OpMod == 0 (N/A), INMmod == Number of VPM entries
 * -------------------------------------------------
 * The MAP_FA command passes physical pages to the switch. These pages
 * are used to store the device firmware. MAP_FA can be executed multiple
 * times until all the firmware area is mapped (the size that should be
 * mapped is retrieved through the QUERY_FW command). All required pages
 * must be mapped to finish the initialization phase. Physical memory
 * passed in this command must be pinned.
 */

#define MLXSW_CMD_MAP_FA_VPM_ENTRIES_MAX 32

static inline int mlxsw_cmd_map_fa(struct mlxsw_core *mlxsw_core,
				   char *in_mbox, u32 vpm_entries_count)
{
	return mlxsw_cmd_exec_in(mlxsw_core, MLXSW_CMD_OPCODE_MAP_FA,
				 0, vpm_entries_count,
				 in_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_map_fa_pa
 * Physical Address.
 */
MLXSW_ITEM64_INDEXED(cmd_mbox, map_fa, pa, 0x00, 12, 52, 0x08, 0x00, true);

/* cmd_mbox_map_fa_log2size
 * Log (base 2) of the size in 4KB pages of the physical and contiguous memory
 * that starts at PA_L/H.
 */
MLXSW_ITEM32_INDEXED(cmd_mbox, map_fa, log2size, 0x00, 0, 5, 0x08, 0x04, false);

/* UNMAP_FA - Unmap Firmware Area
 * ------------------------------
 * OpMod == 0 (N/A), INMmod == 0 (N/A)
 * -----------------------------------
 * The UNMAP_FA command unload the firmware and unmaps all the
 * firmware area. After this command is completed the device will not access
 * the pages that were mapped to the firmware area. After executing UNMAP_FA
 * command, software reset must be done prior to execution of MAP_FW command.
 */

static inline int mlxsw_cmd_unmap_fa(struct mlxsw_core *mlxsw_core)
{
	return mlxsw_cmd_exec_none(mlxsw_core, MLXSW_CMD_OPCODE_UNMAP_FA, 0, 0);
}

/* QUERY_RESOURCES - Query chip resources
 * --------------------------------------
 * OpMod == 0 (N/A) , INMmod is index
 * ----------------------------------
 * The QUERY_RESOURCES command retrieves information related to chip resources
 * by resource ID. Every command returns 32 entries. INmod is being use as base.
 * for example, index 1 will return entries 32-63. When the tables end and there
 * are no more sources in the table, will return resource id 0xFFF to indicate
 * it.
 */

#define MLXSW_CMD_QUERY_RESOURCES_TABLE_END_ID 0xffff
#define MLXSW_CMD_QUERY_RESOURCES_MAX_QUERIES 100
#define MLXSW_CMD_QUERY_RESOURCES_PER_QUERY 32

static inline int mlxsw_cmd_query_resources(struct mlxsw_core *mlxsw_core,
					    char *out_mbox, int index)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_QUERY_RESOURCES,
				  0, index, false, out_mbox,
				  MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_query_resource_id
 * The resource id. 0xFFFF indicates table's end.
 */
MLXSW_ITEM32_INDEXED(cmd_mbox, query_resource, id, 0x00, 16, 16, 0x8, 0, false);

/* cmd_mbox_query_resource_data
 * The resource
 */
MLXSW_ITEM64_INDEXED(cmd_mbox, query_resource, data,
		     0x00, 0, 40, 0x8, 0, false);

/* CONFIG_PROFILE (Set) - Configure Switch Profile
 * ------------------------------
 * OpMod == 1 (Set), INMmod == 0 (N/A)
 * -----------------------------------
 * The CONFIG_PROFILE command sets the switch profile. The command can be
 * executed on the device only once at startup in order to allocate and
 * configure all switch resources and prepare it for operational mode.
 * It is not possible to change the device profile after the chip is
 * in operational mode.
 * Failure of the CONFIG_PROFILE command leaves the hardware in an indeterminate
 * state therefore it is required to perform software reset to the device
 * following an unsuccessful completion of the command. It is required
 * to perform software reset to the device to change an existing profile.
 */

static inline int mlxsw_cmd_config_profile_set(struct mlxsw_core *mlxsw_core,
					       char *in_mbox)
{
	return mlxsw_cmd_exec_in(mlxsw_core, MLXSW_CMD_OPCODE_CONFIG_PROFILE,
				 1, 0, in_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_config_profile_set_max_vepa_channels
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_vepa_channels, 0x0C, 0, 1);

/* cmd_mbox_config_profile_set_max_lag
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_lag, 0x0C, 1, 1);

/* cmd_mbox_config_profile_set_max_port_per_lag
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_port_per_lag, 0x0C, 2, 1);

/* cmd_mbox_config_profile_set_max_mid
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_mid, 0x0C, 3, 1);

/* cmd_mbox_config_profile_set_max_pgt
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_pgt, 0x0C, 4, 1);

/* cmd_mbox_config_profile_set_max_system_port
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_system_port, 0x0C, 5, 1);

/* cmd_mbox_config_profile_set_max_vlan_groups
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_vlan_groups, 0x0C, 6, 1);

/* cmd_mbox_config_profile_set_max_regions
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_regions, 0x0C, 7, 1);

/* cmd_mbox_config_profile_set_flood_mode
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_flood_mode, 0x0C, 8, 1);

/* cmd_mbox_config_profile_set_max_flood_tables
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_flood_tables, 0x0C, 9, 1);

/* cmd_mbox_config_profile_set_max_ib_mc
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_ib_mc, 0x0C, 12, 1);

/* cmd_mbox_config_profile_set_max_pkey
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_max_pkey, 0x0C, 13, 1);

/* cmd_mbox_config_profile_set_adaptive_routing_group_cap
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile,
	     set_adaptive_routing_group_cap, 0x0C, 14, 1);

/* cmd_mbox_config_profile_set_ar_sec
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_ar_sec, 0x0C, 15, 1);

/* cmd_mbox_config_set_kvd_linear_size
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_kvd_linear_size, 0x0C, 24, 1);

/* cmd_mbox_config_set_kvd_hash_single_size
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_kvd_hash_single_size, 0x0C, 25, 1);

/* cmd_mbox_config_set_kvd_hash_double_size
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_kvd_hash_double_size, 0x0C, 26, 1);

/* cmd_mbox_config_set_cqe_version
 * Capability bit. Setting a bit to 1 configures the profile
 * according to the mailbox contents.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, set_cqe_version, 0x08, 0, 1);

/* cmd_mbox_config_profile_max_vepa_channels
 * Maximum number of VEPA channels per port (0 through 16)
 * 0 - multi-channel VEPA is disabled
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_vepa_channels, 0x10, 0, 8);

/* cmd_mbox_config_profile_max_lag
 * Maximum number of LAG IDs requested.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_lag, 0x14, 0, 16);

/* cmd_mbox_config_profile_max_port_per_lag
 * Maximum number of ports per LAG requested.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_port_per_lag, 0x18, 0, 16);

/* cmd_mbox_config_profile_max_mid
 * Maximum Multicast IDs.
 * Multicast IDs are allocated from 0 to max_mid-1
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_mid, 0x1C, 0, 16);

/* cmd_mbox_config_profile_max_pgt
 * Maximum records in the Port Group Table per Switch Partition.
 * Port Group Table indexes are from 0 to max_pgt-1
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_pgt, 0x20, 0, 16);

/* cmd_mbox_config_profile_max_system_port
 * The maximum number of system ports that can be allocated.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_system_port, 0x24, 0, 16);

/* cmd_mbox_config_profile_max_vlan_groups
 * Maximum number VLAN Groups for VLAN binding.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_vlan_groups, 0x28, 0, 12);

/* cmd_mbox_config_profile_max_regions
 * Maximum number of TCAM Regions.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_regions, 0x2C, 0, 16);

/* cmd_mbox_config_profile_max_flood_tables
 * Maximum number of single-entry flooding tables. Different flooding tables
 * can be associated with different packet types.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_flood_tables, 0x30, 16, 4);

/* cmd_mbox_config_profile_max_vid_flood_tables
 * Maximum number of per-vid flooding tables. Flooding tables are associated
 * to the different packet types for the different switch partitions.
 * Table size is 4K entries covering all VID space.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_vid_flood_tables, 0x30, 8, 4);

/* cmd_mbox_config_profile_flood_mode
 * Flooding mode to use.
 * 0-2 - Backward compatible modes for SwitchX devices.
 * 3 - Mixed mode, where:
 * max_flood_tables indicates the number of single-entry tables.
 * max_vid_flood_tables indicates the number of per-VID tables.
 * max_fid_offset_flood_tables indicates the number of FID-offset tables.
 * max_fid_flood_tables indicates the number of per-FID tables.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, flood_mode, 0x30, 0, 2);

/* cmd_mbox_config_profile_max_fid_offset_flood_tables
 * Maximum number of FID-offset flooding tables.
 */
MLXSW_ITEM32(cmd_mbox, config_profile,
	     max_fid_offset_flood_tables, 0x34, 24, 4);

/* cmd_mbox_config_profile_fid_offset_flood_table_size
 * The size (number of entries) of each FID-offset flood table.
 */
MLXSW_ITEM32(cmd_mbox, config_profile,
	     fid_offset_flood_table_size, 0x34, 0, 16);

/* cmd_mbox_config_profile_max_fid_flood_tables
 * Maximum number of per-FID flooding tables.
 *
 * Note: This flooding tables cover special FIDs only (vFIDs), starting at
 * FID value 4K and higher.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_fid_flood_tables, 0x38, 24, 4);

/* cmd_mbox_config_profile_fid_flood_table_size
 * The size (number of entries) of each per-FID table.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, fid_flood_table_size, 0x38, 0, 16);

/* cmd_mbox_config_profile_max_ib_mc
 * Maximum number of multicast FDB records for InfiniBand
 * FDB (in 512 chunks) per InfiniBand switch partition.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_ib_mc, 0x40, 0, 15);

/* cmd_mbox_config_profile_max_pkey
 * Maximum per port PKEY table size (for PKEY enforcement)
 */
MLXSW_ITEM32(cmd_mbox, config_profile, max_pkey, 0x44, 0, 15);

/* cmd_mbox_config_profile_ar_sec
 * Primary/secondary capability
 * Describes the number of adaptive routing sub-groups
 * 0 - disable primary/secondary (single group)
 * 1 - enable primary/secondary (2 sub-groups)
 * 2 - 3 sub-groups: Not supported in SwitchX, SwitchX-2
 * 3 - 4 sub-groups: Not supported in SwitchX, SwitchX-2
 */
MLXSW_ITEM32(cmd_mbox, config_profile, ar_sec, 0x4C, 24, 2);

/* cmd_mbox_config_profile_adaptive_routing_group_cap
 * Adaptive Routing Group Capability. Indicates the number of AR groups
 * supported. Note that when Primary/secondary is enabled, each
 * primary/secondary couple consumes 2 adaptive routing entries.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, adaptive_routing_group_cap, 0x4C, 0, 16);

/* cmd_mbox_config_profile_arn
 * Adaptive Routing Notification Enable
 * Not supported in SwitchX, SwitchX-2
 */
MLXSW_ITEM32(cmd_mbox, config_profile, arn, 0x50, 31, 1);

/* cmd_mbox_config_kvd_linear_size
 * KVD Linear Size
 * Valid for Spectrum only
 * Allowed values are 128*N where N=0 or higher
 */
MLXSW_ITEM32(cmd_mbox, config_profile, kvd_linear_size, 0x54, 0, 24);

/* cmd_mbox_config_kvd_hash_single_size
 * KVD Hash single-entries size
 * Valid for Spectrum only
 * Allowed values are 128*N where N=0 or higher
 * Must be greater or equal to cap_min_kvd_hash_single_size
 * Must be smaller or equal to cap_kvd_size - kvd_linear_size
 */
MLXSW_ITEM32(cmd_mbox, config_profile, kvd_hash_single_size, 0x58, 0, 24);

/* cmd_mbox_config_kvd_hash_double_size
 * KVD Hash double-entries size (units of single-size entries)
 * Valid for Spectrum only
 * Allowed values are 128*N where N=0 or higher
 * Must be either 0 or greater or equal to cap_min_kvd_hash_double_size
 * Must be smaller or equal to cap_kvd_size - kvd_linear_size
 */
MLXSW_ITEM32(cmd_mbox, config_profile, kvd_hash_double_size, 0x5C, 0, 24);

/* cmd_mbox_config_profile_swid_config_mask
 * Modify Switch Partition Configuration mask. When set, the configu-
 * ration value for the Switch Partition are taken from the mailbox.
 * When clear, the current configuration values are used.
 * Bit 0 - set type
 * Bit 1 - properties
 * Other - reserved
 */
MLXSW_ITEM32_INDEXED(cmd_mbox, config_profile, swid_config_mask,
		     0x60, 24, 8, 0x08, 0x00, false);

/* cmd_mbox_config_profile_swid_config_type
 * Switch Partition type.
 * 0000 - disabled (Switch Partition does not exist)
 * 0001 - InfiniBand
 * 0010 - Ethernet
 * 1000 - router port (SwitchX-2 only)
 * Other - reserved
 */
MLXSW_ITEM32_INDEXED(cmd_mbox, config_profile, swid_config_type,
		     0x60, 20, 4, 0x08, 0x00, false);

/* cmd_mbox_config_profile_swid_config_properties
 * Switch Partition properties.
 */
MLXSW_ITEM32_INDEXED(cmd_mbox, config_profile, swid_config_properties,
		     0x60, 0, 8, 0x08, 0x00, false);

/* cmd_mbox_config_profile_cqe_version
 * CQE version:
 * 0: CQE version is 0
 * 1: CQE version is either 1 or 2
 * CQE ver 1 or 2 is configured by Completion Queue Context field cqe_ver.
 */
MLXSW_ITEM32(cmd_mbox, config_profile, cqe_version, 0xB0, 0, 8);

/* ACCESS_REG - Access EMAD Supported Register
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == 0 (N/A)
 * -------------------------------------
 * The ACCESS_REG command supports accessing device registers. This access
 * is mainly used for bootstrapping.
 */

static inline int mlxsw_cmd_access_reg(struct mlxsw_core *mlxsw_core,
				       bool reset_ok,
				       char *in_mbox, char *out_mbox)
{
	return mlxsw_cmd_exec(mlxsw_core, MLXSW_CMD_OPCODE_ACCESS_REG,
			      0, 0, false, reset_ok,
			      in_mbox, MLXSW_CMD_MBOX_SIZE,
			      out_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* SW2HW_DQ - Software to Hardware DQ
 * ----------------------------------
 * OpMod == 0 (send DQ) / OpMod == 1 (receive DQ)
 * INMmod == DQ number
 * ----------------------------------------------
 * The SW2HW_DQ command transitions a descriptor queue from software to
 * hardware ownership. The command enables posting WQEs and ringing DoorBells
 * on the descriptor queue.
 */

static inline int __mlxsw_cmd_sw2hw_dq(struct mlxsw_core *mlxsw_core,
				       char *in_mbox, u32 dq_number,
				       u8 opcode_mod)
{
	return mlxsw_cmd_exec_in(mlxsw_core, MLXSW_CMD_OPCODE_SW2HW_DQ,
				 opcode_mod, dq_number,
				 in_mbox, MLXSW_CMD_MBOX_SIZE);
}

enum {
	MLXSW_CMD_OPCODE_MOD_SDQ = 0,
	MLXSW_CMD_OPCODE_MOD_RDQ = 1,
};

static inline int mlxsw_cmd_sw2hw_sdq(struct mlxsw_core *mlxsw_core,
				      char *in_mbox, u32 dq_number)
{
	return __mlxsw_cmd_sw2hw_dq(mlxsw_core, in_mbox, dq_number,
				    MLXSW_CMD_OPCODE_MOD_SDQ);
}

static inline int mlxsw_cmd_sw2hw_rdq(struct mlxsw_core *mlxsw_core,
				      char *in_mbox, u32 dq_number)
{
	return __mlxsw_cmd_sw2hw_dq(mlxsw_core, in_mbox, dq_number,
				    MLXSW_CMD_OPCODE_MOD_RDQ);
}

/* cmd_mbox_sw2hw_dq_cq
 * Number of the CQ that this Descriptor Queue reports completions to.
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_dq, cq, 0x00, 24, 8);

/* cmd_mbox_sw2hw_dq_sdq_tclass
 * SDQ: CPU Egress TClass
 * RDQ: Reserved
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_dq, sdq_tclass, 0x00, 16, 6);

/* cmd_mbox_sw2hw_dq_log2_dq_sz
 * Log (base 2) of the Descriptor Queue size in 4KB pages.
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_dq, log2_dq_sz, 0x00, 0, 6);

/* cmd_mbox_sw2hw_dq_pa
 * Physical Address.
 */
MLXSW_ITEM64_INDEXED(cmd_mbox, sw2hw_dq, pa, 0x10, 12, 52, 0x08, 0x00, true);

/* HW2SW_DQ - Hardware to Software DQ
 * ----------------------------------
 * OpMod == 0 (send DQ) / OpMod == 1 (receive DQ)
 * INMmod == DQ number
 * ----------------------------------------------
 * The HW2SW_DQ command transitions a descriptor queue from hardware to
 * software ownership. Incoming packets on the DQ are silently discarded,
 * SW should not post descriptors on nonoperational DQs.
 */

static inline int __mlxsw_cmd_hw2sw_dq(struct mlxsw_core *mlxsw_core,
				       u32 dq_number, u8 opcode_mod)
{
	return mlxsw_cmd_exec_none(mlxsw_core, MLXSW_CMD_OPCODE_HW2SW_DQ,
				   opcode_mod, dq_number);
}

static inline int mlxsw_cmd_hw2sw_sdq(struct mlxsw_core *mlxsw_core,
				      u32 dq_number)
{
	return __mlxsw_cmd_hw2sw_dq(mlxsw_core, dq_number,
				    MLXSW_CMD_OPCODE_MOD_SDQ);
}

static inline int mlxsw_cmd_hw2sw_rdq(struct mlxsw_core *mlxsw_core,
				      u32 dq_number)
{
	return __mlxsw_cmd_hw2sw_dq(mlxsw_core, dq_number,
				    MLXSW_CMD_OPCODE_MOD_RDQ);
}

/* 2ERR_DQ - To Error DQ
 * ---------------------
 * OpMod == 0 (send DQ) / OpMod == 1 (receive DQ)
 * INMmod == DQ number
 * ----------------------------------------------
 * The 2ERR_DQ command transitions the DQ into the error state from the state
 * in which it has been. While the command is executed, some in-process
 * descriptors may complete. Once the DQ transitions into the error state,
 * if there are posted descriptors on the RDQ/SDQ, the hardware writes
 * a completion with error (flushed) for all descriptors posted in the RDQ/SDQ.
 * When the command is completed successfully, the DQ is already in
 * the error state.
 */

static inline int __mlxsw_cmd_2err_dq(struct mlxsw_core *mlxsw_core,
				      u32 dq_number, u8 opcode_mod)
{
	return mlxsw_cmd_exec_none(mlxsw_core, MLXSW_CMD_OPCODE_2ERR_DQ,
				   opcode_mod, dq_number);
}

static inline int mlxsw_cmd_2err_sdq(struct mlxsw_core *mlxsw_core,
				     u32 dq_number)
{
	return __mlxsw_cmd_2err_dq(mlxsw_core, dq_number,
				   MLXSW_CMD_OPCODE_MOD_SDQ);
}

static inline int mlxsw_cmd_2err_rdq(struct mlxsw_core *mlxsw_core,
				     u32 dq_number)
{
	return __mlxsw_cmd_2err_dq(mlxsw_core, dq_number,
				   MLXSW_CMD_OPCODE_MOD_RDQ);
}

/* QUERY_DQ - Query DQ
 * ---------------------
 * OpMod == 0 (send DQ) / OpMod == 1 (receive DQ)
 * INMmod == DQ number
 * ----------------------------------------------
 * The QUERY_DQ command retrieves a snapshot of DQ parameters from the hardware.
 *
 * Note: Output mailbox has the same format as SW2HW_DQ.
 */

static inline int __mlxsw_cmd_query_dq(struct mlxsw_core *mlxsw_core,
				       char *out_mbox, u32 dq_number,
				       u8 opcode_mod)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_2ERR_DQ,
				  opcode_mod, dq_number, false,
				  out_mbox, MLXSW_CMD_MBOX_SIZE);
}

static inline int mlxsw_cmd_query_sdq(struct mlxsw_core *mlxsw_core,
				      char *out_mbox, u32 dq_number)
{
	return __mlxsw_cmd_query_dq(mlxsw_core, out_mbox, dq_number,
				    MLXSW_CMD_OPCODE_MOD_SDQ);
}

static inline int mlxsw_cmd_query_rdq(struct mlxsw_core *mlxsw_core,
				      char *out_mbox, u32 dq_number)
{
	return __mlxsw_cmd_query_dq(mlxsw_core, out_mbox, dq_number,
				    MLXSW_CMD_OPCODE_MOD_RDQ);
}

/* SW2HW_CQ - Software to Hardware CQ
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == CQ number
 * -------------------------------------
 * The SW2HW_CQ command transfers ownership of a CQ context entry from software
 * to hardware. The command takes the CQ context entry from the input mailbox
 * and stores it in the CQC in the ownership of the hardware. The command fails
 * if the requested CQC entry is already in the ownership of the hardware.
 */

static inline int mlxsw_cmd_sw2hw_cq(struct mlxsw_core *mlxsw_core,
				     char *in_mbox, u32 cq_number)
{
	return mlxsw_cmd_exec_in(mlxsw_core, MLXSW_CMD_OPCODE_SW2HW_CQ,
				 0, cq_number, in_mbox, MLXSW_CMD_MBOX_SIZE);
}

enum mlxsw_cmd_mbox_sw2hw_cq_cqe_ver {
	MLXSW_CMD_MBOX_SW2HW_CQ_CQE_VER_1,
	MLXSW_CMD_MBOX_SW2HW_CQ_CQE_VER_2,
};

/* cmd_mbox_sw2hw_cq_cqe_ver
 * CQE Version.
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_cq, cqe_ver, 0x00, 28, 4);

/* cmd_mbox_sw2hw_cq_c_eqn
 * Event Queue this CQ reports completion events to.
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_cq, c_eqn, 0x00, 24, 1);

/* cmd_mbox_sw2hw_cq_st
 * Event delivery state machine
 * 0x0 - FIRED
 * 0x1 - ARMED (Request for Notification)
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_cq, st, 0x00, 8, 1);

/* cmd_mbox_sw2hw_cq_log_cq_size
 * Log (base 2) of the CQ size (in entries).
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_cq, log_cq_size, 0x00, 0, 4);

/* cmd_mbox_sw2hw_cq_producer_counter
 * Producer Counter. The counter is incremented for each CQE that is
 * written by the HW to the CQ.
 * Maintained by HW (valid for the QUERY_CQ command only)
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_cq, producer_counter, 0x04, 0, 16);

/* cmd_mbox_sw2hw_cq_pa
 * Physical Address.
 */
MLXSW_ITEM64_INDEXED(cmd_mbox, sw2hw_cq, pa, 0x10, 11, 53, 0x08, 0x00, true);

/* HW2SW_CQ - Hardware to Software CQ
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == CQ number
 * -------------------------------------
 * The HW2SW_CQ command transfers ownership of a CQ context entry from hardware
 * to software. The CQC entry is invalidated as a result of this command.
 */

static inline int mlxsw_cmd_hw2sw_cq(struct mlxsw_core *mlxsw_core,
				     u32 cq_number)
{
	return mlxsw_cmd_exec_none(mlxsw_core, MLXSW_CMD_OPCODE_HW2SW_CQ,
				   0, cq_number);
}

/* QUERY_CQ - Query CQ
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == CQ number
 * -------------------------------------
 * The QUERY_CQ command retrieves a snapshot of the current CQ context entry.
 * The command stores the snapshot in the output mailbox in the software format.
 * Note that the CQ context state and values are not affected by the QUERY_CQ
 * command. The QUERY_CQ command is for debug purposes only.
 *
 * Note: Output mailbox has the same format as SW2HW_CQ.
 */

static inline int mlxsw_cmd_query_cq(struct mlxsw_core *mlxsw_core,
				     char *out_mbox, u32 cq_number)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_QUERY_CQ,
				  0, cq_number, false,
				  out_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* SW2HW_EQ - Software to Hardware EQ
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == EQ number
 * -------------------------------------
 * The SW2HW_EQ command transfers ownership of an EQ context entry from software
 * to hardware. The command takes the EQ context entry from the input mailbox
 * and stores it in the EQC in the ownership of the hardware. The command fails
 * if the requested EQC entry is already in the ownership of the hardware.
 */

static inline int mlxsw_cmd_sw2hw_eq(struct mlxsw_core *mlxsw_core,
				     char *in_mbox, u32 eq_number)
{
	return mlxsw_cmd_exec_in(mlxsw_core, MLXSW_CMD_OPCODE_SW2HW_EQ,
				 0, eq_number, in_mbox, MLXSW_CMD_MBOX_SIZE);
}

/* cmd_mbox_sw2hw_eq_int_msix
 * When set, MSI-X cycles will be generated by this EQ.
 * When cleared, an interrupt will be generated by this EQ.
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_eq, int_msix, 0x00, 24, 1);

/* cmd_mbox_sw2hw_eq_st
 * Event delivery state machine
 * 0x0 - FIRED
 * 0x1 - ARMED (Request for Notification)
 * 0x11 - Always ARMED
 * other - reserved
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_eq, st, 0x00, 8, 2);

/* cmd_mbox_sw2hw_eq_log_eq_size
 * Log (base 2) of the EQ size (in entries).
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_eq, log_eq_size, 0x00, 0, 4);

/* cmd_mbox_sw2hw_eq_producer_counter
 * Producer Counter. The counter is incremented for each EQE that is written
 * by the HW to the EQ.
 * Maintained by HW (valid for the QUERY_EQ command only)
 */
MLXSW_ITEM32(cmd_mbox, sw2hw_eq, producer_counter, 0x04, 0, 16);

/* cmd_mbox_sw2hw_eq_pa
 * Physical Address.
 */
MLXSW_ITEM64_INDEXED(cmd_mbox, sw2hw_eq, pa, 0x10, 11, 53, 0x08, 0x00, true);

/* HW2SW_EQ - Hardware to Software EQ
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == EQ number
 * -------------------------------------
 */

static inline int mlxsw_cmd_hw2sw_eq(struct mlxsw_core *mlxsw_core,
				     u32 eq_number)
{
	return mlxsw_cmd_exec_none(mlxsw_core, MLXSW_CMD_OPCODE_HW2SW_EQ,
				   0, eq_number);
}

/* QUERY_EQ - Query EQ
 * ----------------------------------
 * OpMod == 0 (N/A), INMmod == EQ number
 * -------------------------------------
 *
 * Note: Output mailbox has the same format as SW2HW_EQ.
 */

static inline int mlxsw_cmd_query_eq(struct mlxsw_core *mlxsw_core,
				     char *out_mbox, u32 eq_number)
{
	return mlxsw_cmd_exec_out(mlxsw_core, MLXSW_CMD_OPCODE_QUERY_EQ,
				  0, eq_number, false,
				  out_mbox, MLXSW_CMD_MBOX_SIZE);
}

#endif
