// SPDX-License-Identifier: GPL-2.0
/*
 * Automatically generated functions to read TDX global metadata.
 *
 * This file doesn't compile on its own as it lacks of inclusion
 * of SEAMCALL wrapper primitive which reads global metadata.
 * Include this file to other C file instead.
 */

static int get_tdx_sys_info_features(struct tdx_sys_info_features *sysinfo_features)
{
	int ret = 0;
	u64 val;

	if (!ret && !(ret = read_sys_metadata_field(0x0A00000300000008, &val)))
		sysinfo_features->tdx_features0 = val;

	return ret;
}

static int get_tdx_sys_info_tdmr(struct tdx_sys_info_tdmr *sysinfo_tdmr)
{
	int ret = 0;
	u64 val;

	if (!ret && !(ret = read_sys_metadata_field(0x9100000100000008, &val)))
		sysinfo_tdmr->max_tdmrs = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9100000100000009, &val)))
		sysinfo_tdmr->max_reserved_per_tdmr = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9100000100000010, &val)))
		sysinfo_tdmr->pamt_4k_entry_size = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9100000100000011, &val)))
		sysinfo_tdmr->pamt_2m_entry_size = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9100000100000012, &val)))
		sysinfo_tdmr->pamt_1g_entry_size = val;

	return ret;
}

static int get_tdx_sys_info(struct tdx_sys_info *sysinfo)
{
	int ret = 0;

	ret = ret ?: get_tdx_sys_info_features(&sysinfo->features);
	ret = ret ?: get_tdx_sys_info_tdmr(&sysinfo->tdmr);

	return ret;
}
