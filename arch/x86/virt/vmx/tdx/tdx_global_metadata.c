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

static int get_tdx_sys_info_td_ctrl(struct tdx_sys_info_td_ctrl *sysinfo_td_ctrl)
{
	int ret = 0;
	u64 val;

	if (!ret && !(ret = read_sys_metadata_field(0x9800000100000000, &val)))
		sysinfo_td_ctrl->tdr_base_size = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9800000100000100, &val)))
		sysinfo_td_ctrl->tdcs_base_size = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9800000100000200, &val)))
		sysinfo_td_ctrl->tdvps_base_size = val;

	return ret;
}

static int get_tdx_sys_info_td_conf(struct tdx_sys_info_td_conf *sysinfo_td_conf)
{
	int ret = 0;
	u64 val;
	int i, j;

	if (!ret && !(ret = read_sys_metadata_field(0x1900000300000000, &val)))
		sysinfo_td_conf->attributes_fixed0 = val;
	if (!ret && !(ret = read_sys_metadata_field(0x1900000300000001, &val)))
		sysinfo_td_conf->attributes_fixed1 = val;
	if (!ret && !(ret = read_sys_metadata_field(0x1900000300000002, &val)))
		sysinfo_td_conf->xfam_fixed0 = val;
	if (!ret && !(ret = read_sys_metadata_field(0x1900000300000003, &val)))
		sysinfo_td_conf->xfam_fixed1 = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9900000100000004, &val)))
		sysinfo_td_conf->num_cpuid_config = val;
	if (!ret && !(ret = read_sys_metadata_field(0x9900000100000008, &val)))
		sysinfo_td_conf->max_vcpus_per_td = val;
	if (sysinfo_td_conf->num_cpuid_config > ARRAY_SIZE(sysinfo_td_conf->cpuid_config_leaves))
		return -EINVAL;
	for (i = 0; i < sysinfo_td_conf->num_cpuid_config; i++)
		if (!ret && !(ret = read_sys_metadata_field(0x9900000300000400 + i, &val)))
			sysinfo_td_conf->cpuid_config_leaves[i] = val;
	if (sysinfo_td_conf->num_cpuid_config > ARRAY_SIZE(sysinfo_td_conf->cpuid_config_values))
		return -EINVAL;
	for (i = 0; i < sysinfo_td_conf->num_cpuid_config; i++)
		for (j = 0; j < 2; j++)
			if (!ret && !(ret = read_sys_metadata_field(0x9900000300000500 + i * 2 + j, &val)))
				sysinfo_td_conf->cpuid_config_values[i][j] = val;

	return ret;
}

static int get_tdx_sys_info(struct tdx_sys_info *sysinfo)
{
	int ret = 0;

	ret = ret ?: get_tdx_sys_info_features(&sysinfo->features);
	ret = ret ?: get_tdx_sys_info_tdmr(&sysinfo->tdmr);
	ret = ret ?: get_tdx_sys_info_td_ctrl(&sysinfo->td_ctrl);
	ret = ret ?: get_tdx_sys_info_td_conf(&sysinfo->td_conf);

	return ret;
}
