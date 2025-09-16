// SPDX-License-Identifier: GPL-2.0-only
/*
 * Resource Director Technology(RDT)
 * - Cache Allocation code.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Authors:
 *    Fenghua Yu <fenghua.yu@intel.com>
 *    Tony Luck <tony.luck@intel.com>
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual June 2016, volume 3, section 17.17.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>

#include "internal.h"

int resctrl_arch_update_one(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type t, u32 cfg_val)
{
	struct rdt_hw_ctrl_domain *hw_dom = resctrl_to_arch_ctrl_dom(d);
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	u32 idx = resctrl_get_config_index(closid, t);
	struct msr_param msr_param;

	if (!cpumask_test_cpu(smp_processor_id(), &d->hdr.cpu_mask))
		return -EINVAL;

	hw_dom->ctrl_val[idx] = cfg_val;

	msr_param.res = r;
	msr_param.dom = d;
	msr_param.low = idx;
	msr_param.high = idx + 1;
	hw_res->msr_update(&msr_param);

	return 0;
}

int resctrl_arch_update_domains(struct rdt_resource *r, u32 closid)
{
	struct resctrl_staged_config *cfg;
	struct rdt_hw_ctrl_domain *hw_dom;
	struct msr_param msr_param;
	struct rdt_ctrl_domain *d;
	enum resctrl_conf_type t;
	u32 idx;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
		hw_dom = resctrl_to_arch_ctrl_dom(d);
		msr_param.res = NULL;
		for (t = 0; t < CDP_NUM_TYPES; t++) {
			cfg = &hw_dom->d_resctrl.staged_config[t];
			if (!cfg->have_new_ctrl)
				continue;

			idx = resctrl_get_config_index(closid, t);
			if (cfg->new_ctrl == hw_dom->ctrl_val[idx])
				continue;
			hw_dom->ctrl_val[idx] = cfg->new_ctrl;

			if (!msr_param.res) {
				msr_param.low = idx;
				msr_param.high = msr_param.low + 1;
				msr_param.res = r;
				msr_param.dom = d;
			} else {
				msr_param.low = min(msr_param.low, idx);
				msr_param.high = max(msr_param.high, idx + 1);
			}
		}
		if (msr_param.res)
			smp_call_function_any(&d->hdr.cpu_mask, rdt_ctrl_update, &msr_param, 1);
	}

	return 0;
}

u32 resctrl_arch_get_config(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type type)
{
	struct rdt_hw_ctrl_domain *hw_dom = resctrl_to_arch_ctrl_dom(d);
	u32 idx = resctrl_get_config_index(closid, type);

	return hw_dom->ctrl_val[idx];
}
