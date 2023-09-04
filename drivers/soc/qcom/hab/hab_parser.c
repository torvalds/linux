// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2018, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include <linux/of.h>

/*
 * set valid mmid value in tbl to show this is valid entry. All inputs here are
 * normalized to 1 based integer
 */
static int fill_vmid_mmid_tbl(struct vmid_mmid_desc *tbl, int32_t vm_start,
				   int32_t vm_range, int32_t mmid_start,
				   int32_t mmid_range, int32_t be, int kernel_only)
{
	int i, j;

	for (i = vm_start; i < vm_start+vm_range; i++) {
		tbl[i].vmid = i; /* set valid vmid value to make it usable */
		for (j = mmid_start; j < mmid_start + mmid_range; j++) {
			/* sanity check */
			if (tbl[i].mmid[j] != HABCFG_VMID_INVALID) {
				pr_err("overwrite previous setting vmid %d, mmid %d, be %d, kernel only %d\n",
					i, j, tbl[i].is_listener[j], tbl[i].kernel_only[j]);
			}
			tbl[i].mmid[j] = j;
			tbl[i].is_listener[j] = be; /* BE IS listen */
			tbl[i].kernel_only[j] = kernel_only;
		}
	}

	return 0;
}

void dump_settings(struct local_vmid *settings)
{
	pr_debug("self vmid is %d\n", settings->self);
}


#ifdef CONFIG_MSM_VHOST_HAB
int fill_default_gvm_settings(struct local_vmid *settings, int vmid_default,
		int mmid_start, int mmid_end)
{
	int32_t be = HABCFG_BE_TRUE;
	int32_t range = 1;
	int32_t vmremote = vmid_default;
	/* pchan is not kernel only by default */
	int32_t kernel_only = 0;

	/* default gvm always talks to host as vm0 */
	settings->self = 0;
	return fill_vmid_mmid_tbl(settings->vmid_mmid_list, vmremote, range,
		mmid_start/100, (mmid_end-mmid_start)/100+1, be, kernel_only);
}
#else
int fill_default_gvm_settings(struct local_vmid *settings, int vmid_local,
		int mmid_start, int mmid_end)
{
	int32_t be = HABCFG_BE_FALSE;
	int32_t range = 1;
	int32_t vmremote = 0; /* default to host[0] as local is guest[2] */
	/* pchan is not kernel only by default */
	int32_t kernel_only = 0;

	settings->self = vmid_local;
	/* default gvm always talks to host as vm0 */
	return fill_vmid_mmid_tbl(settings->vmid_mmid_list, vmremote, range,
		mmid_start/100, (mmid_end-mmid_start)/100+1, be, kernel_only);
}
#endif

/* device tree based parser */
static int hab_parse_dt(struct local_vmid *settings)
{
	int result, i;
	struct device_node *hab_node = NULL;
	struct device_node *mmid_grp_node = NULL;
	const char *role = NULL;
	int tmp = -1, vmids_num;
	u32 vmids[16];
	int32_t grp_start_id, be;
	int kernel_only;

	/* parse device tree*/
	pr_debug("parsing hab node in device tree...\n");
	hab_node = of_find_compatible_node(NULL, NULL, "qcom,hab");
	if (!hab_node) {
		pr_err("no hab device tree node\n");
		return -ENODEV;
	}

	/* read the local vmid of this VM, like 0 for host, 1 for AGL GVM */
	result = of_property_read_u32(hab_node, "vmid", &tmp);
	if (result) {
		pr_err("failed to read local vmid, result = %d\n", result);
		return result;
	}

	pr_debug("local vmid = %d\n", tmp);
	settings->self = tmp;

	for_each_child_of_node(hab_node, mmid_grp_node) {
		/* read the group starting id */
		result = of_property_read_u32(mmid_grp_node,
				"grp-start-id", &tmp);
		if (result) {
			pr_err("failed to read grp-start-id, result = %d\n",
				result);
			return result;
		}

		pr_debug("grp-start-id = %d\n", tmp);
		grp_start_id = tmp;

		/* read the role(fe/be) of these pchans in this mmid group */
		result = of_property_read_string(mmid_grp_node, "role", &role);
		if (result) {
			pr_err("failed to get role, result = %d\n", result);
			return result;
		}

		pr_debug("local role of this mmid group is %s\n", role);
		if (!strcmp(role, "be"))
			be = 1;
		else
			be = 0;

		/* read the remote vmids for these pchans in this mmid group */
		vmids_num = of_property_count_elems_of_size(mmid_grp_node,
					"remote-vmids", sizeof(u32));

		result = of_property_read_u32_array(mmid_grp_node,
					"remote-vmids", vmids, vmids_num);
		if (result) {
			pr_err("failed to read remote-vmids, result = %d\n",
				result);
			return result;
		}

		/* check the kernel_only flag for these pchans in this mmid group */
		result = of_property_read_bool(mmid_grp_node, "kernel_only");
		if (result) {
			kernel_only = 1;
			pr_debug("kernel_only flag is set for this mmid group\n");
		} else {
			kernel_only = 0;
			pr_debug("kernel_only flag is not set for this mmid group\n");
		}

		for (i = 0; i < vmids_num; i++) {
			pr_debug("vmids_num = %d, vmids[%d] = %d\n",
				vmids_num, i, vmids[i]);

			result = fill_vmid_mmid_tbl(
					settings->vmid_mmid_list,
					vmids[i], 1,
					grp_start_id/100, 1, be, kernel_only);
			if (result) {
				pr_err("fill_vmid_mmid_tbl failed\n");
				return result;
			}
		}

	}

	dump_settings(settings);
	return 0;
}

/*
 * 0: successful
 * negative: various failure core
 */
int hab_parse(struct local_vmid *settings)
{
	int ret;

	ret = hab_parse_dt(settings);

	return ret;
}
