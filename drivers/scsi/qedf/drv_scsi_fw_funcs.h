/* SPDX-License-Identifier: GPL-2.0-only */
/* QLogic FCoE Offload Driver
 * Copyright (c) 2016-2018 Cavium Inc.
 */
#ifndef _SCSI_FW_FUNCS_H
#define _SCSI_FW_FUNCS_H
#include <linux/qed/common_hsi.h>
#include <linux/qed/storage_common.h>
#include <linux/qed/fcoe_common.h>

struct scsi_sgl_task_params {
	struct scsi_sge *sgl;
	struct regpair sgl_phys_addr;
	u32 total_buffer_size;
	u16 num_sges;

	 /* true if SGL contains a small (< 4KB) SGE in middle(not 1st or last)
	  * -> relevant for tx only
	  */
	bool small_mid_sge;
};

struct scsi_dif_task_params {
	u32 initial_ref_tag;
	bool initial_ref_tag_is_valid;
	u16 application_tag;
	u16 application_tag_mask;
	u16 dif_block_size_log;
	bool dif_on_network;
	bool dif_on_host;
	u8 host_guard_type;
	u8 protection_type;
	u8 ref_tag_mask;
	bool crc_seed;

	 /* Enable Connection error upon DIF error (segments with DIF errors are
	  * dropped)
	  */
	bool tx_dif_conn_err_en;
	bool ignore_app_tag;
	bool keep_ref_tag_const;
	bool validate_guard;
	bool validate_app_tag;
	bool validate_ref_tag;
	bool forward_guard;
	bool forward_app_tag;
	bool forward_ref_tag;
	bool forward_app_tag_with_mask;
	bool forward_ref_tag_with_mask;
};

struct scsi_initiator_cmd_params {
	 /* for cdb_size > default CDB size (extended CDB > 16 bytes) ->
	  * pointer to the CDB buffer SGE
	  */
	struct scsi_sge extended_cdb_sge;

	/* Physical address of sense data buffer for sense data - 256B buffer */
	struct regpair sense_data_buffer_phys_addr;
};

/**
 * @brief scsi_is_slow_sgl - checks for slow SGL
 *
 * @param num_sges - number of sges in SGL
 * @param small_mid_sge - True is the SGL contains an SGE which is smaller than
 * 4KB and its not the 1st or last SGE in the SGL
 */
bool scsi_is_slow_sgl(u16 num_sges, bool small_mid_sge);

/**
 * @brief init_scsi_sgl_context - initializes SGL task context
 *
 * @param sgl_params - SGL context parameters to initialize (output parameter)
 * @param data_desc - context struct containing SGEs array to set (output
 * parameter)
 * @param sgl_task_params - SGL parameters (input)
 */
void init_scsi_sgl_context(struct scsi_sgl_params *sgl_params,
	struct scsi_cached_sges *ctx_data_desc,
	struct scsi_sgl_task_params *sgl_task_params);
#endif
