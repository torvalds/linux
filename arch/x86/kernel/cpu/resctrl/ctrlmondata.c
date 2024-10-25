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
#include <linux/kernfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/tick.h>

#include "internal.h"

/*
 * Check whether MBA bandwidth percentage value is correct. The value is
 * checked against the minimum and max bandwidth values specified by the
 * hardware. The allocated bandwidth percentage is rounded to the next
 * control step available on the hardware.
 */
static bool bw_validate(char *buf, u32 *data, struct rdt_resource *r)
{
	int ret;
	u32 bw;

	/*
	 * Only linear delay values is supported for current Intel SKUs.
	 */
	if (!r->membw.delay_linear && r->membw.arch_needs_linear) {
		rdt_last_cmd_puts("No support for non-linear MB domains\n");
		return false;
	}

	ret = kstrtou32(buf, 10, &bw);
	if (ret) {
		rdt_last_cmd_printf("Invalid MB value %s\n", buf);
		return false;
	}

	/* Nothing else to do if software controller is enabled. */
	if (is_mba_sc(r)) {
		*data = bw;
		return true;
	}

	if (bw < r->membw.min_bw || bw > r->default_ctrl) {
		rdt_last_cmd_printf("MB value %u out of range [%d,%d]\n",
				    bw, r->membw.min_bw, r->default_ctrl);
		return false;
	}

	*data = roundup(bw, (unsigned long)r->membw.bw_gran);
	return true;
}

int parse_bw(struct rdt_parse_data *data, struct resctrl_schema *s,
	     struct rdt_ctrl_domain *d)
{
	struct resctrl_staged_config *cfg;
	u32 closid = data->rdtgrp->closid;
	struct rdt_resource *r = s->res;
	u32 bw_val;

	cfg = &d->staged_config[s->conf_type];
	if (cfg->have_new_ctrl) {
		rdt_last_cmd_printf("Duplicate domain %d\n", d->hdr.id);
		return -EINVAL;
	}

	if (!bw_validate(data->buf, &bw_val, r))
		return -EINVAL;

	if (is_mba_sc(r)) {
		d->mbps_val[closid] = bw_val;
		return 0;
	}

	cfg->new_ctrl = bw_val;
	cfg->have_new_ctrl = true;

	return 0;
}

/*
 * Check whether a cache bit mask is valid.
 * On Intel CPUs, non-contiguous 1s value support is indicated by CPUID:
 *   - CPUID.0x10.1:ECX[3]: L3 non-contiguous 1s value supported if 1
 *   - CPUID.0x10.2:ECX[3]: L2 non-contiguous 1s value supported if 1
 *
 * Haswell does not support a non-contiguous 1s value and additionally
 * requires at least two bits set.
 * AMD allows non-contiguous bitmasks.
 */
static bool cbm_validate(char *buf, u32 *data, struct rdt_resource *r)
{
	unsigned long first_bit, zero_bit, val;
	unsigned int cbm_len = r->cache.cbm_len;
	int ret;

	ret = kstrtoul(buf, 16, &val);
	if (ret) {
		rdt_last_cmd_printf("Non-hex character in the mask %s\n", buf);
		return false;
	}

	if ((r->cache.min_cbm_bits > 0 && val == 0) || val > r->default_ctrl) {
		rdt_last_cmd_puts("Mask out of range\n");
		return false;
	}

	first_bit = find_first_bit(&val, cbm_len);
	zero_bit = find_next_zero_bit(&val, cbm_len, first_bit);

	/* Are non-contiguous bitmasks allowed? */
	if (!r->cache.arch_has_sparse_bitmasks &&
	    (find_next_bit(&val, cbm_len, zero_bit) < cbm_len)) {
		rdt_last_cmd_printf("The mask %lx has non-consecutive 1-bits\n", val);
		return false;
	}

	if ((zero_bit - first_bit) < r->cache.min_cbm_bits) {
		rdt_last_cmd_printf("Need at least %d bits in the mask\n",
				    r->cache.min_cbm_bits);
		return false;
	}

	*data = val;
	return true;
}

/*
 * Read one cache bit mask (hex). Check that it is valid for the current
 * resource type.
 */
int parse_cbm(struct rdt_parse_data *data, struct resctrl_schema *s,
	      struct rdt_ctrl_domain *d)
{
	struct rdtgroup *rdtgrp = data->rdtgrp;
	struct resctrl_staged_config *cfg;
	struct rdt_resource *r = s->res;
	u32 cbm_val;

	cfg = &d->staged_config[s->conf_type];
	if (cfg->have_new_ctrl) {
		rdt_last_cmd_printf("Duplicate domain %d\n", d->hdr.id);
		return -EINVAL;
	}

	/*
	 * Cannot set up more than one pseudo-locked region in a cache
	 * hierarchy.
	 */
	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP &&
	    rdtgroup_pseudo_locked_in_hierarchy(d)) {
		rdt_last_cmd_puts("Pseudo-locked region in hierarchy\n");
		return -EINVAL;
	}

	if (!cbm_validate(data->buf, &cbm_val, r))
		return -EINVAL;

	if ((rdtgrp->mode == RDT_MODE_EXCLUSIVE ||
	     rdtgrp->mode == RDT_MODE_SHAREABLE) &&
	    rdtgroup_cbm_overlaps_pseudo_locked(d, cbm_val)) {
		rdt_last_cmd_puts("CBM overlaps with pseudo-locked region\n");
		return -EINVAL;
	}

	/*
	 * The CBM may not overlap with the CBM of another closid if
	 * either is exclusive.
	 */
	if (rdtgroup_cbm_overlaps(s, d, cbm_val, rdtgrp->closid, true)) {
		rdt_last_cmd_puts("Overlaps with exclusive group\n");
		return -EINVAL;
	}

	if (rdtgroup_cbm_overlaps(s, d, cbm_val, rdtgrp->closid, false)) {
		if (rdtgrp->mode == RDT_MODE_EXCLUSIVE ||
		    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
			rdt_last_cmd_puts("Overlaps with other group\n");
			return -EINVAL;
		}
	}

	cfg->new_ctrl = cbm_val;
	cfg->have_new_ctrl = true;

	return 0;
}

/*
 * For each domain in this resource we expect to find a series of:
 *	id=mask
 * separated by ";". The "id" is in decimal, and must match one of
 * the "id"s for this resource.
 */
static int parse_line(char *line, struct resctrl_schema *s,
		      struct rdtgroup *rdtgrp)
{
	enum resctrl_conf_type t = s->conf_type;
	struct resctrl_staged_config *cfg;
	struct rdt_resource *r = s->res;
	struct rdt_parse_data data;
	struct rdt_ctrl_domain *d;
	char *dom = NULL, *id;
	unsigned long dom_id;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP &&
	    (r->rid == RDT_RESOURCE_MBA || r->rid == RDT_RESOURCE_SMBA)) {
		rdt_last_cmd_puts("Cannot pseudo-lock MBA resource\n");
		return -EINVAL;
	}

next:
	if (!line || line[0] == '\0')
		return 0;
	dom = strsep(&line, ";");
	id = strsep(&dom, "=");
	if (!dom || kstrtoul(id, 10, &dom_id)) {
		rdt_last_cmd_puts("Missing '=' or non-numeric domain\n");
		return -EINVAL;
	}
	dom = strim(dom);
	list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
		if (d->hdr.id == dom_id) {
			data.buf = dom;
			data.rdtgrp = rdtgrp;
			if (r->parse_ctrlval(&data, s, d))
				return -EINVAL;
			if (rdtgrp->mode ==  RDT_MODE_PSEUDO_LOCKSETUP) {
				cfg = &d->staged_config[t];
				/*
				 * In pseudo-locking setup mode and just
				 * parsed a valid CBM that should be
				 * pseudo-locked. Only one locked region per
				 * resource group and domain so just do
				 * the required initialization for single
				 * region and return.
				 */
				rdtgrp->plr->s = s;
				rdtgrp->plr->d = d;
				rdtgrp->plr->cbm = cfg->new_ctrl;
				d->plr = rdtgrp->plr;
				return 0;
			}
			goto next;
		}
	}
	return -EINVAL;
}

static u32 get_config_index(u32 closid, enum resctrl_conf_type type)
{
	switch (type) {
	default:
	case CDP_NONE:
		return closid;
	case CDP_CODE:
		return closid * 2 + 1;
	case CDP_DATA:
		return closid * 2;
	}
}

int resctrl_arch_update_one(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type t, u32 cfg_val)
{
	struct rdt_hw_ctrl_domain *hw_dom = resctrl_to_arch_ctrl_dom(d);
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	u32 idx = get_config_index(closid, t);
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

			idx = get_config_index(closid, t);
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

static int rdtgroup_parse_resource(char *resname, char *tok,
				   struct rdtgroup *rdtgrp)
{
	struct resctrl_schema *s;

	list_for_each_entry(s, &resctrl_schema_all, list) {
		if (!strcmp(resname, s->name) && rdtgrp->closid < s->num_closid)
			return parse_line(tok, s, rdtgrp);
	}
	rdt_last_cmd_printf("Unknown or unsupported resource name '%s'\n", resname);
	return -EINVAL;
}

ssize_t rdtgroup_schemata_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct resctrl_schema *s;
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;
	char *tok, *resname;
	int ret = 0;

	/* Valid input requires a trailing newline */
	if (nbytes == 0 || buf[nbytes - 1] != '\n')
		return -EINVAL;
	buf[nbytes - 1] = '\0';

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		rdtgroup_kn_unlock(of->kn);
		return -ENOENT;
	}
	rdt_last_cmd_clear();

	/*
	 * No changes to pseudo-locked region allowed. It has to be removed
	 * and re-created instead.
	 */
	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED) {
		ret = -EINVAL;
		rdt_last_cmd_puts("Resource group is pseudo-locked\n");
		goto out;
	}

	rdt_staged_configs_clear();

	while ((tok = strsep(&buf, "\n")) != NULL) {
		resname = strim(strsep(&tok, ":"));
		if (!tok) {
			rdt_last_cmd_puts("Missing ':'\n");
			ret = -EINVAL;
			goto out;
		}
		if (tok[0] == '\0') {
			rdt_last_cmd_printf("Missing '%s' value\n", resname);
			ret = -EINVAL;
			goto out;
		}
		ret = rdtgroup_parse_resource(resname, tok, rdtgrp);
		if (ret)
			goto out;
	}

	list_for_each_entry(s, &resctrl_schema_all, list) {
		r = s->res;

		/*
		 * Writes to mba_sc resources update the software controller,
		 * not the control MSR.
		 */
		if (is_mba_sc(r))
			continue;

		ret = resctrl_arch_update_domains(r, rdtgrp->closid);
		if (ret)
			goto out;
	}

	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
		/*
		 * If pseudo-locking fails we keep the resource group in
		 * mode RDT_MODE_PSEUDO_LOCKSETUP with its class of service
		 * active and updated for just the domain the pseudo-locked
		 * region was requested for.
		 */
		ret = rdtgroup_pseudo_lock_create(rdtgrp);
	}

out:
	rdt_staged_configs_clear();
	rdtgroup_kn_unlock(of->kn);
	return ret ?: nbytes;
}

u32 resctrl_arch_get_config(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type type)
{
	struct rdt_hw_ctrl_domain *hw_dom = resctrl_to_arch_ctrl_dom(d);
	u32 idx = get_config_index(closid, type);

	return hw_dom->ctrl_val[idx];
}

static void show_doms(struct seq_file *s, struct resctrl_schema *schema, int closid)
{
	struct rdt_resource *r = schema->res;
	struct rdt_ctrl_domain *dom;
	bool sep = false;
	u32 ctrl_val;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	seq_printf(s, "%*s:", max_name_width, schema->name);
	list_for_each_entry(dom, &r->ctrl_domains, hdr.list) {
		if (sep)
			seq_puts(s, ";");

		if (is_mba_sc(r))
			ctrl_val = dom->mbps_val[closid];
		else
			ctrl_val = resctrl_arch_get_config(r, dom, closid,
							   schema->conf_type);

		seq_printf(s, r->format_str, dom->hdr.id, max_data_width,
			   ctrl_val);
		sep = true;
	}
	seq_puts(s, "\n");
}

int rdtgroup_schemata_show(struct kernfs_open_file *of,
			   struct seq_file *s, void *v)
{
	struct resctrl_schema *schema;
	struct rdtgroup *rdtgrp;
	int ret = 0;
	u32 closid;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (rdtgrp) {
		if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
			list_for_each_entry(schema, &resctrl_schema_all, list) {
				seq_printf(s, "%s:uninitialized\n", schema->name);
			}
		} else if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED) {
			if (!rdtgrp->plr->d) {
				rdt_last_cmd_clear();
				rdt_last_cmd_puts("Cache domain offline\n");
				ret = -ENODEV;
			} else {
				seq_printf(s, "%s:%d=%x\n",
					   rdtgrp->plr->s->res->name,
					   rdtgrp->plr->d->hdr.id,
					   rdtgrp->plr->cbm);
			}
		} else {
			closid = rdtgrp->closid;
			list_for_each_entry(schema, &resctrl_schema_all, list) {
				if (closid < schema->num_closid)
					show_doms(s, schema, closid);
			}
		}
	} else {
		ret = -ENOENT;
	}
	rdtgroup_kn_unlock(of->kn);
	return ret;
}

static int smp_mon_event_count(void *arg)
{
	mon_event_count(arg);

	return 0;
}

void mon_event_read(struct rmid_read *rr, struct rdt_resource *r,
		    struct rdt_mon_domain *d, struct rdtgroup *rdtgrp,
		    cpumask_t *cpumask, int evtid, int first)
{
	int cpu;

	/* When picking a CPU from cpu_mask, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	/*
	 * Setup the parameters to pass to mon_event_count() to read the data.
	 */
	rr->rgrp = rdtgrp;
	rr->evtid = evtid;
	rr->r = r;
	rr->d = d;
	rr->first = first;
	rr->arch_mon_ctx = resctrl_arch_mon_ctx_alloc(r, evtid);
	if (IS_ERR(rr->arch_mon_ctx)) {
		rr->err = -EINVAL;
		return;
	}

	cpu = cpumask_any_housekeeping(cpumask, RESCTRL_PICK_ANY_CPU);

	/*
	 * cpumask_any_housekeeping() prefers housekeeping CPUs, but
	 * are all the CPUs nohz_full? If yes, pick a CPU to IPI.
	 * MPAM's resctrl_arch_rmid_read() is unable to read the
	 * counters on some platforms if its called in IRQ context.
	 */
	if (tick_nohz_full_cpu(cpu))
		smp_call_function_any(cpumask, mon_event_count, rr, 1);
	else
		smp_call_on_cpu(cpu, smp_mon_event_count, rr, false);

	resctrl_arch_mon_ctx_free(r, evtid, rr->arch_mon_ctx);
}

int rdtgroup_mondata_show(struct seq_file *m, void *arg)
{
	struct kernfs_open_file *of = m->private;
	struct rdt_domain_hdr *hdr;
	struct rmid_read rr = {0};
	struct rdt_mon_domain *d;
	u32 resid, evtid, domid;
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;
	union mon_data_bits md;
	int ret = 0;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (!rdtgrp) {
		ret = -ENOENT;
		goto out;
	}

	md.priv = of->kn->priv;
	resid = md.u.rid;
	domid = md.u.domid;
	evtid = md.u.evtid;
	r = &rdt_resources_all[resid].r_resctrl;

	if (md.u.sum) {
		/*
		 * This file requires summing across all domains that share
		 * the L3 cache id that was provided in the "domid" field of the
		 * mon_data_bits union. Search all domains in the resource for
		 * one that matches this cache id.
		 */
		list_for_each_entry(d, &r->mon_domains, hdr.list) {
			if (d->ci->id == domid) {
				rr.ci = d->ci;
				mon_event_read(&rr, r, NULL, rdtgrp,
					       &d->ci->shared_cpu_map, evtid, false);
				goto checkresult;
			}
		}
		ret = -ENOENT;
		goto out;
	} else {
		/*
		 * This file provides data from a single domain. Search
		 * the resource to find the domain with "domid".
		 */
		hdr = rdt_find_domain(&r->mon_domains, domid, NULL);
		if (!hdr || WARN_ON_ONCE(hdr->type != RESCTRL_MON_DOMAIN)) {
			ret = -ENOENT;
			goto out;
		}
		d = container_of(hdr, struct rdt_mon_domain, hdr);
		mon_event_read(&rr, r, d, rdtgrp, &d->hdr.cpu_mask, evtid, false);
	}

checkresult:

	if (rr.err == -EIO)
		seq_puts(m, "Error\n");
	else if (rr.err == -EINVAL)
		seq_puts(m, "Unavailable\n");
	else
		seq_printf(m, "%llu\n", rr.val);

out:
	rdtgroup_kn_unlock(of->kn);
	return ret;
}
