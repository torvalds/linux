/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#ifndef __iwl_fw_runtime_h__
#define __iwl_fw_runtime_h__

#include "iwl-config.h"
#include "iwl-trans.h"
#include "img.h"
#include "fw/api/debug.h"
#include "fw/api/paging.h"
#include "fw/api/power.h"
#include "iwl-nvm-utils.h"
#include "fw/acpi.h"
#include "fw/regulatory.h"

struct iwl_fw_runtime_ops {
	void (*dump_start)(void *ctx);
	void (*dump_end)(void *ctx);
	int (*send_hcmd)(void *ctx, struct iwl_host_cmd *host_cmd);
	bool (*d3_debug_enable)(void *ctx);
};

#define MAX_NUM_LMAC 2
#define MAX_NUM_TCM 2
#define MAX_NUM_RCM 2
struct iwl_fwrt_shared_mem_cfg {
	int num_lmacs;
	int num_txfifo_entries;
	struct {
		u32 txfifo_size[TX_FIFO_MAX_NUM];
		u32 rxfifo1_size;
	} lmac[MAX_NUM_LMAC];
	u32 rxfifo2_size;
	u32 rxfifo2_control_size;
	u32 internal_txfifo_addr;
	u32 internal_txfifo_size[TX_FIFO_INTERNAL_MAX_NUM];
};

#define IWL_FW_RUNTIME_DUMP_WK_NUM 5

/**
 * struct iwl_fwrt_dump_data - dump data
 * @trig: trigger the worker was scheduled upon
 * @fw_pkt: packet received from FW
 *
 * Note that the decision which part of the union is used
 * is based on iwl_trans_dbg_ini_valid(): the 'trig' part
 * is used if it is %true, the 'desc' part otherwise.
 */
struct iwl_fwrt_dump_data {
	union {
		struct {
			struct iwl_fw_ini_trigger_tlv *trig;
			struct iwl_rx_packet *fw_pkt;
		};
		struct {
			/* must be first to be same as 'trig' */
			const struct iwl_fw_dump_desc *desc;
			bool monitor_only;
		};
	};
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
 * @dump: debug dump data
 * @uats_table: AP type table
 * @uats_valid: is AP type table valid
 * @uefi_tables_lock_status: The status of the WIFI GUID UEFI variables lock:
 *	0: Unlocked, 1 and 2: Locked.
 *	Only read the UEFI variables if locked.
 * @sar_profiles: sar profiles as read from WRDS/EWRD BIOS tables
 * @geo_profiles: geographic profiles as read from WGDS BIOS table
 * @phy_filters: specific phy filters as read from WPFC BIOS table
 */
struct iwl_fw_runtime {
	struct iwl_trans *trans;
	const struct iwl_fw *fw;
	struct device *dev;

	const struct iwl_fw_runtime_ops *ops;
	void *ops_ctx;

	const struct iwl_dump_sanitize_ops *sanitize_ops;
	void *sanitize_ctx;

	/* Paging */
	struct iwl_fw_paging fw_paging_db[NUM_OF_FW_PAGING_BLOCKS];
	u16 num_of_paging_blk;
	u16 num_of_pages_in_last_blk;

	enum iwl_ucode_type cur_fw_img;

	/* memory configuration */
	struct iwl_fwrt_shared_mem_cfg smem_cfg;

	/* debug */
	struct {
		struct iwl_fwrt_wk_data wks[IWL_FW_RUNTIME_DUMP_WK_NUM];
		unsigned long active_wks;

		u8 conf;

		/* ts of the beginning of a non-collect fw dbg data period */
		unsigned long non_collect_ts_start[IWL_FW_INI_TIME_POINT_NUM];
		u32 *d3_debug_data;
		u32 lmac_err_id[MAX_NUM_LMAC];
		u32 tcm_err_id[MAX_NUM_TCM];
		u32 rcm_err_id[MAX_NUM_RCM];
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
	struct {
#ifdef CONFIG_IWLWIFI_DEBUGFS
		struct delayed_work wk;
		u32 delay;
#endif
		u64 seq;
	} timestamp;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	bool tpc_enabled;
#endif /* CONFIG_IWLWIFI_DEBUGFS */
	struct iwl_sar_profile sar_profiles[BIOS_SAR_MAX_PROFILE_NUM];
	u8 sar_chain_a_profile;
	u8 sar_chain_b_profile;
	u8 reduced_power_flags;
	struct iwl_geo_profile geo_profiles[BIOS_GEO_MAX_PROFILE_NUM];
	u32 geo_rev;
	u32 geo_num_profiles;
	bool geo_enabled;
	struct iwl_ppag_chain ppag_chains[IWL_NUM_CHAIN_LIMITS];
	u32 ppag_flags;
	u8 ppag_ver;
	struct iwl_sar_offset_mapping_cmd sgom_table;
	bool sgom_enabled;
	struct iwl_mcc_allowed_ap_type_cmd uats_table;
	bool uats_valid;
	u8 uefi_tables_lock_status;
	struct iwl_phy_specific_cfg phy_filters;
};

void iwl_fw_runtime_init(struct iwl_fw_runtime *fwrt, struct iwl_trans *trans,
			const struct iwl_fw *fw,
			const struct iwl_fw_runtime_ops *ops, void *ops_ctx,
			const struct iwl_dump_sanitize_ops *sanitize_ops,
			void *sanitize_ctx,
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
int iwl_set_soc_latency(struct iwl_fw_runtime *fwrt);
int iwl_configure_rxq(struct iwl_fw_runtime *fwrt);

#endif /* __iwl_fw_runtime_h__ */
