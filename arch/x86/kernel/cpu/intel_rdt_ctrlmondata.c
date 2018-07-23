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
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual June 2016, volume 3, section 17.17.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "intel_rdt.h"

/*
 * Check whether MBA bandwidth percentage value is correct. The value is
 * checked against the minimum and max bandwidth values specified by the
 * hardware. The allocated bandwidth percentage is rounded to the next
 * control step available on the hardware.
 */
static bool bw_validate(char *buf, unsigned long *data, struct rdt_resource *r)
{
	unsigned long bw;
	int ret;

	/*
	 * Only linear delay values is supported for current Intel SKUs.
	 */
	if (!r->membw.delay_linear) {
		rdt_last_cmd_puts("No support for non-linear MB domains\n");
		return false;
	}

	ret = kstrtoul(buf, 10, &bw);
	if (ret) {
		rdt_last_cmd_printf("Non-decimal digit in MB value %s\n", buf);
		return false;
	}

	if ((bw < r->membw.min_bw || bw > r->default_ctrl) &&
	    !is_mba_sc(r)) {
		rdt_last_cmd_printf("MB value %ld out of range [%d,%d]\n", bw,
				    r->membw.min_bw, r->default_ctrl);
		return false;
	}

	*data = roundup(bw, (unsigned long)r->membw.bw_gran);
	return true;
}

int parse_bw(char *buf, struct rdt_resource *r, struct rdt_domain *d)
{
	unsigned long data;

	if (d->have_new_ctrl) {
		rdt_last_cmd_printf("duplicate domain %d\n", d->id);
		return -EINVAL;
	}

	if (!bw_validate(buf, &data, r))
		return -EINVAL;
	d->new_ctrl = data;
	d->have_new_ctrl = true;

	return 0;
}

/*
 * Check whether a cache bit mask is valid. The SDM says:
 *	Please note that all (and only) contiguous '1' combinations
 *	are allowed (e.g. FFFFH, 0FF0H, 003CH, etc.).
 * Additionally Haswell requires at least two bits set.
 */
static bool cbm_validate(char *buf, unsigned long *data, struct rdt_resource *r)
{
	unsigned long first_bit, zero_bit, val;
	unsigned int cbm_len = r->cache.cbm_len;
	int ret;

	ret = kstrtoul(buf, 16, &val);
	if (ret) {
		rdt_last_cmd_printf("non-hex character in mask %s\n", buf);
		return false;
	}

	if (val == 0 || val > r->default_ctrl) {
		rdt_last_cmd_puts("mask out of range\n");
		return false;
	}

	first_bit = find_first_bit(&val, cbm_len);
	zero_bit = find_next_zero_bit(&val, cbm_len, first_bit);

	if (find_next_bit(&val, cbm_len, zero_bit) < cbm_len) {
		rdt_last_cmd_printf("mask %lx has non-consecutive 1-bits\n", val);
		return false;
	}

	if ((zero_bit - first_bit) < r->cache.min_cbm_bits) {
		rdt_last_cmd_printf("Need at least %d bits in mask\n",
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
int parse_cbm(char *buf, struct rdt_resource *r, struct rdt_domain *d)
{
	unsigned long data;

	if (d->have_new_ctrl) {
		rdt_last_cmd_printf("duplicate domain %d\n", d->id);
		return -EINVAL;
	}

	if(!cbm_validate(buf, &data, r))
		return -EINVAL;
	d->new_ctrl = data;
	d->have_new_ctrl = true;

	return 0;
}

/*
 * For each domain in this resource we expect to find a series of:
 *	id=mask
 * separated by ";". The "id" is in decimal, and must match one of
 * the "id"s for this resource.
 */
static int parse_line(char *line, struct rdt_resource *r)
{
	char *dom = NULL, *id;
	struct rdt_domain *d;
	unsigned long dom_id;

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
	list_for_each_entry(d, &r->domains, list) {
		if (d->id == dom_id) {
			if (r->parse_ctrlval(dom, r, d))
				return -EINVAL;
			goto next;
		}
	}
	return -EINVAL;
}

static int update_domains(struct rdt_resource *r, int closid)
{
	struct msr_param msr_param;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	bool mba_sc;
	u32 *dc;
	int cpu;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	msr_param.low = closid;
	msr_param.high = msr_param.low + 1;
	msr_param.res = r;

	mba_sc = is_mba_sc(r);
	list_for_each_entry(d, &r->domains, list) {
		dc = !mba_sc ? d->ctrl_val : d->mbps_val;
		if (d->have_new_ctrl && d->new_ctrl != dc[closid]) {
			cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);
			dc[closid] = d->new_ctrl;
		}
	}

	/*
	 * Avoid writing the control msr with control values when
	 * MBA software controller is enabled
	 */
	if (cpumask_empty(cpu_mask) || mba_sc)
		goto done;
	cpu = get_cpu();
	/* Update CBM on this cpu if it's in cpu_mask. */
	if (cpumask_test_cpu(cpu, cpu_mask))
		rdt_ctrl_update(&msr_param);
	/* Update CBM on other cpus. */
	smp_call_function_many(cpu_mask, rdt_ctrl_update, &msr_param, 1);
	put_cpu();

done:
	free_cpumask_var(cpu_mask);

	return 0;
}

static int rdtgroup_parse_resource(char *resname, char *tok, int closid)
{
	struct rdt_resource *r;

	for_each_alloc_enabled_rdt_resource(r) {
		if (!strcmp(resname, r->name) && closid < r->num_closid)
			return parse_line(tok, r);
	}
	rdt_last_cmd_printf("unknown/unsupported resource name '%s'\n", resname);
	return -EINVAL;
}

ssize_t rdtgroup_schemata_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct rdtgroup *rdtgrp;
	struct rdt_domain *dom;
	struct rdt_resource *r;
	char *tok, *resname;
	int closid, ret = 0;

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

	closid = rdtgrp->closid;

	for_each_alloc_enabled_rdt_resource(r) {
		list_for_each_entry(dom, &r->domains, list)
			dom->have_new_ctrl = false;
	}

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
		ret = rdtgroup_parse_resource(resname, tok, closid);
		if (ret)
			goto out;
	}

	for_each_alloc_enabled_rdt_resource(r) {
		ret = update_domains(r, closid);
		if (ret)
			goto out;
	}

out:
	rdtgroup_kn_unlock(of->kn);
	return ret ?: nbytes;
}

static void show_doms(struct seq_file *s, struct rdt_resource *r, int closid)
{
	struct rdt_domain *dom;
	bool sep = false;
	u32 ctrl_val;

	seq_printf(s, "%*s:", max_name_width, r->name);
	list_for_each_entry(dom, &r->domains, list) {
		if (sep)
			seq_puts(s, ";");

		ctrl_val = (!is_mba_sc(r) ? dom->ctrl_val[closid] :
			    dom->mbps_val[closid]);
		seq_printf(s, r->format_str, dom->id, max_data_width,
			   ctrl_val);
		sep = true;
	}
	seq_puts(s, "\n");
}

int rdtgroup_schemata_show(struct kernfs_open_file *of,
			   struct seq_file *s, void *v)
{
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;
	int ret = 0;
	u32 closid;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);
	if (rdtgrp) {
		closid = rdtgrp->closid;
		for_each_alloc_enabled_rdt_resource(r) {
			if (closid < r->num_closid)
				show_doms(s, r, closid);
		}
	} else {
		ret = -ENOENT;
	}
	rdtgroup_kn_unlock(of->kn);
	return ret;
}

void mon_event_read(struct rmid_read *rr, struct rdt_domain *d,
		    struct rdtgroup *rdtgrp, int evtid, int first)
{
	/*
	 * setup the parameters to send to the IPI to read the data.
	 */
	rr->rgrp = rdtgrp;
	rr->evtid = evtid;
	rr->d = d;
	rr->val = 0;
	rr->first = first;

	smp_call_function_any(&d->cpu_mask, mon_event_count, rr, 1);
}

int rdtgroup_mondata_show(struct seq_file *m, void *arg)
{
	struct kernfs_open_file *of = m->private;
	u32 resid, evtid, domid;
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;
	union mon_data_bits md;
	struct rdt_domain *d;
	struct rmid_read rr;
	int ret = 0;

	rdtgrp = rdtgroup_kn_lock_live(of->kn);

	md.priv = of->kn->priv;
	resid = md.u.rid;
	domid = md.u.domid;
	evtid = md.u.evtid;

	r = &rdt_resources_all[resid];
	d = rdt_find_domain(r, domid, NULL);
	if (!d) {
		ret = -ENOENT;
		goto out;
	}

	mon_event_read(&rr, d, rdtgrp, evtid, false);

	if (rr.val & RMID_VAL_ERROR)
		seq_puts(m, "Error\n");
	else if (rr.val & RMID_VAL_UNAVAIL)
		seq_puts(m, "Unavailable\n");
	else
		seq_printf(m, "%llu\n", rr.val * r->mon_scale);

out:
	rdtgroup_kn_unlock(of->kn);
	return ret;
}
