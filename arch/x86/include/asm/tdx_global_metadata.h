/* SPDX-License-Identifier: GPL-2.0 */
/* Automatically generated TDX global metadata structures. */
#ifndef _X86_VIRT_TDX_AUTO_GENERATED_TDX_GLOBAL_METADATA_H
#define _X86_VIRT_TDX_AUTO_GENERATED_TDX_GLOBAL_METADATA_H

#include <linux/types.h>

struct tdx_sys_info_features {
	u64 tdx_features0;
};

struct tdx_sys_info_tdmr {
	u16 max_tdmrs;
	u16 max_reserved_per_tdmr;
	u16 pamt_4k_entry_size;
	u16 pamt_2m_entry_size;
	u16 pamt_1g_entry_size;
};

struct tdx_sys_info_td_ctrl {
	u16 tdr_base_size;
	u16 tdcs_base_size;
	u16 tdvps_base_size;
};

struct tdx_sys_info_td_conf {
	u64 attributes_fixed0;
	u64 attributes_fixed1;
	u64 xfam_fixed0;
	u64 xfam_fixed1;
	u16 num_cpuid_config;
	u16 max_vcpus_per_td;
	u64 cpuid_config_leaves[128];
	u64 cpuid_config_values[128][2];
};

struct tdx_sys_info {
	struct tdx_sys_info_features features;
	struct tdx_sys_info_tdmr tdmr;
	struct tdx_sys_info_td_ctrl td_ctrl;
	struct tdx_sys_info_td_conf td_conf;
};

#endif
