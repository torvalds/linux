/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2019 Intel Corporation
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2019 Intel Corporation
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
#ifndef __iwl_fw_runtime_h__
#define __iwl_fw_runtime_h__

#include "iwl-config.h"
#include "iwl-trans.h"
#include "img.h"
#include "fw/api/debug.h"
#include "fw/api/paging.h"
#include "fw/api/power.h"
#include "iwl-eeprom-parse.h"
#include "fw/acpi.h"

#define IWL_FW_DBG_DOMAIN		IWL_FW_INI_DOMAIN_ALWAYS_ON

struct iwl_fw_runtime_ops {
	int (*dump_start)(void *ctx);
	void (*dump_end)(void *ctx);
	bool (*fw_running)(void *ctx);
	int (*send_hcmd)(void *ctx, struct iwl_host_cmd *host_cmd);
	bool (*d3_debug_enable)(void *ctx);
};

#define MAX_NUM_LMAC 2
struct iwl_fwrt_shared_mem_cfg {
	int num_lmacs;
	int num_txfifo_entries;
	struct {
		u32 txfifo_size[TX_FIFO_MAX_NUM];
		u32 rxfifo1_size;
	} lmac[MAX_NUM_LMAC];
	u32 rxfifo2_size;
	u32 internal_txfifo_addr;
	u32 internal_txfifo_size[TX_FIFO_INTERNAL_MAX_NUM];
};

#define IWL_FW_RUNTIME_DUMP_WK_NUM 5

/**
 * struct iwl_fwrt_dump_data - dump data
 * @trig: trigger the worker was scheduled upon
 * @fw_pkt: packet received from FW
 */
struct iwl_fwrt_dump_data {
	struct iwl_fw_ini_trigger_tlv *trig;
	struct iwl_rx_packet *fw_pkt;
};

/**
 * struct iwl_fwrt_wk_data - dump worker data struct
 * @idx: index of the worker
 * @wk: worker
 */
struct iwl_fwrt_wk_data  {
	u8 idx;
	struct delayed_work wk;
	struct iwl_fwrt_dump_data dump_data;
};

/**
 * struct iwl_txf_iter_data - Tx fifo iterator data struct
 * @fifo: fifo number
 * @lmac: lmac number
 * @fifo_size: fifo size
 * @internal_txf: non zero if fifo is  internal Tx fifo
 */
struct iwl_txf_iter_data {
	int fifo;
	int lmac;
	u32 fifo_size;
	u8 internal_txf;
};

/**
 * enum iwl_fw_runtime_status - fw runtime status flags
 * @STATUS_GEN_ACTIVE_TRIGS: generating active trigger list
 */
enum iwl_fw_runtime_status {
	STATUS_GEN_ACTIVE_TRIGS,
};

/**
 * struct iwl_fw_runtime - runtime data for firmware
 * @fw: firmware image
 * @cfg: NIC configuration
 * @dev: device pointer
 * @ops: user ops
 * @ops_ctx: user ops context
 * @fw_paging_db: paging database
 * @num_of_paging_blk: number of paging blocks
 * @num_of_pages_in_last_blk: number of pages in the last block
 * @smem_cfg: saved firmware SMEM configuration
 * @cur_fw_img: current firmware image, must be maintained by
 *	the driver by calling &iwl_fw_set_current_image()
 * @status: &enum iwl_fw_runtime_status
 * @dump: debug dump data
 */
struct iwl_fw_runtime {
	struct iwl_trans *trans;
	const struct iwl_fw *fw;
	struct device *dev;

	const struct iwl_fw_runtime_ops *ops;
	void *ops_ctx;

	/* Paging */
	struct iwl_fw_paging fw_paging_db[NUM_OF_FW_PAGING_BLOCKS];
	u16 num_of_paging_blk;
	u16 num_of_pages_in_last_blk;

	enum iwl_ucode_type cur_fw_img;

	/* memory configuration */
	struct iwl_fwrt_shared_mem_cfg smem_cfg;

	unsigned long status;

	/* debug */
	struct {
		const struct iwl_fw_dump_desc *desc;
		bool monitor_only;
		struct iwl_fwrt_wk_data wks[IWL_FW_RUNTIME_DUMP_WK_NUM];
		unsigned long active_wks;

		u8 conf;

		/* ts of the beginning of a non-collect fw dbg data period */
		unsigned long non_collect_ts_start[IWL_FW_INI_TIME_POINT_NUM];
		u32 *d3_debug_data;
		u32 lmac_err_id[MAX_NUM_LMAC];
		u32 umac_err_id;

		struct iwl_txf_iter_data txf_iter_data;

		struct {
			u8 type;
			u8 subtype;
			u32 lmac_major;
			u32 lmac_minor;
			u32 umac_major;
			u32 umac_minor;
		} fw_ver;
	} dump;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct {
		struct delayed_work wk;
		u32 delay;
		u64 seq;
	} timestamp;
	bool tpc_enabled;
#endif /* CONFIG_IWLWIFI_DEBUGFS */
#ifdef CONFIG_ACPI
	struct iwl_sar_profile sar_profiles[ACPI_SAR_PROFILE_NUM];
	u8 sar_chain_a_profile;
	u8 sar_chain_b_profile;
	struct iwl_geo_profile geo_profiles[ACPI_NUM_GEO_PROFILES];
	u32 geo_rev;
	struct iwl_ppag_table_cmd ppag_table;
#endif
};

void iwl_fw_runtime_init(struct iwl_fw_runtime *fwrt, struct iwl_trans *trans,
			const struct iwl_fw *fw,
			const struct iwl_fw_runtime_ops *ops, void *ops_ctx,
			struct dentry *dbgfs_dir);

static inline void iwl_fw_runtime_free(struct iwl_fw_runtime *fwrt)
{
	int i;

	kfree(fwrt->dump.d3_debug_data);
	fwrt->dump.d3_debug_data = NULL;

	iwl_dbg_tlv_del_timers(fwrt->trans);
	for (i = 0; i < IWL_FW_RUNTIME_DUMP_WK_NUM; i++)
		cancel_delayed_work_sync(&fwrt->dump.wks[i].wk);
}

void iwl_fw_runtime_suspend(struct iwl_fw_runtime *fwrt);

void iwl_fw_runtime_resume(struct iwl_fw_runtime *fwrt);

static inline void iwl_fw_set_current_image(struct iwl_fw_runtime *fwrt,
					    enum iwl_ucode_type cur_fw_img)
{
	fwrt->cur_fw_img = cur_fw_img;
}

int iwl_init_paging(struct iwl_fw_runtime *fwrt, enum iwl_ucode_type type);
void iwl_free_fw_paging(struct iwl_fw_runtime *fwrt);

void iwl_get_shared_mem_conf(struct iwl_fw_runtime *fwrt);

#endif /* __iwl_fw_runtime_h__ */
