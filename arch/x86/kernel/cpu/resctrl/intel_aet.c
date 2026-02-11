// SPDX-License-Identifier: GPL-2.0-only
/*
 * Resource Director Technology(RDT)
 * - Intel Application Energy Telemetry
 *
 * Copyright (C) 2025 Intel Corporation
 *
 * Author:
 *    Tony Luck <tony.luck@intel.com>
 */

#define pr_fmt(fmt)   "resctrl: " fmt

#include <linux/bits.h>
#include <linux/compiler_types.h>
#include <linux/container_of.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/init.h>
#include <linux/intel_pmt_features.h>
#include <linux/intel_vsec.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/resctrl.h>
#include <linux/resctrl_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/topology.h>
#include <linux/types.h>

#include "internal.h"

/**
 * struct pmt_event - Telemetry event.
 * @id:		Resctrl event id.
 * @idx:	Counter index within each per-RMID block of counters.
 * @bin_bits:	Zero for integer valued events, else number bits in fraction
 *		part of fixed-point.
 */
struct pmt_event {
	enum resctrl_event_id	id;
	unsigned int		idx;
	unsigned int		bin_bits;
};

#define EVT(_id, _idx, _bits) { .id = _id, .idx = _idx, .bin_bits = _bits }

/**
 * struct event_group - Events with the same feature type ("energy" or "perf") and GUID.
 * @pfname:		PMT feature name ("energy" or "perf") of this event group.
 *			Used by boot rdt= option.
 * @pfg:		Points to the aggregated telemetry space information
 *			returned by the intel_pmt_get_regions_by_feature()
 *			call to the INTEL_PMT_TELEMETRY driver that contains
 *			data for all telemetry regions of type @pfname.
 *			Valid if the system supports the event group,
 *			NULL otherwise.
 * @force_off:		True when "rdt" command line or architecture code disables
 *			this event group due to insufficient RMIDs.
 * @force_on:		True when "rdt" command line overrides disable of this
 *			event group.
 * @guid:		Unique number per XML description file.
 * @num_rmid:		Number of RMIDs supported by this group. May be
 *			adjusted downwards if enumeration from
 *			intel_pmt_get_regions_by_feature() indicates fewer
 *			RMIDs can be tracked simultaneously.
 * @mmio_size:		Number of bytes of MMIO registers for this group.
 * @num_events:		Number of events in this group.
 * @evts:		Array of event descriptors.
 */
struct event_group {
	/* Data fields for additional structures to manage this group. */
	const char			*pfname;
	struct pmt_feature_group	*pfg;
	bool				force_off, force_on;

	/* Remaining fields initialized from XML file. */
	u32				guid;
	u32				num_rmid;
	size_t				mmio_size;
	unsigned int			num_events;
	struct pmt_event		evts[] __counted_by(num_events);
};

#define XML_MMIO_SIZE(num_rmids, num_events, num_extra_status) \
		      (((num_rmids) * (num_events) + (num_extra_status)) * sizeof(u64))

/*
 * Link: https://github.com/intel/Intel-PMT/blob/main/xml/CWF/OOBMSM/RMID-ENERGY/cwf_aggregator.xml
 */
static struct event_group energy_0x26696143 = {
	.pfname		= "energy",
	.guid		= 0x26696143,
	.num_rmid	= 576,
	.mmio_size	= XML_MMIO_SIZE(576, 2, 3),
	.num_events	= 2,
	.evts		= {
		EVT(PMT_EVENT_ENERGY, 0, 18),
		EVT(PMT_EVENT_ACTIVITY, 1, 18),
	}
};

/*
 * Link: https://github.com/intel/Intel-PMT/blob/main/xml/CWF/OOBMSM/RMID-PERF/cwf_aggregator.xml
 */
static struct event_group perf_0x26557651 = {
	.pfname		= "perf",
	.guid		= 0x26557651,
	.num_rmid	= 576,
	.mmio_size	= XML_MMIO_SIZE(576, 7, 3),
	.num_events	= 7,
	.evts		= {
		EVT(PMT_EVENT_STALLS_LLC_HIT, 0, 0),
		EVT(PMT_EVENT_C1_RES, 1, 0),
		EVT(PMT_EVENT_UNHALTED_CORE_CYCLES, 2, 0),
		EVT(PMT_EVENT_STALLS_LLC_MISS, 3, 0),
		EVT(PMT_EVENT_AUTO_C6_RES, 4, 0),
		EVT(PMT_EVENT_UNHALTED_REF_CYCLES, 5, 0),
		EVT(PMT_EVENT_UOPS_RETIRED, 6, 0),
	}
};

static struct event_group *known_event_groups[] = {
	&energy_0x26696143,
	&perf_0x26557651,
};

#define for_each_event_group(_peg)						\
	for (_peg = known_event_groups;						\
	     _peg < &known_event_groups[ARRAY_SIZE(known_event_groups)];	\
	     _peg++)

bool intel_handle_aet_option(bool force_off, char *tok)
{
	struct event_group **peg;
	bool ret = false;
	u32 guid = 0;
	char *name;

	if (!tok)
		return false;

	name = strsep(&tok, ":");
	if (tok && kstrtou32(tok, 16, &guid))
		return false;

	for_each_event_group(peg) {
		if (strcmp(name, (*peg)->pfname))
			continue;
		if (guid && (*peg)->guid != guid)
			continue;
		if (force_off)
			(*peg)->force_off = true;
		else
			(*peg)->force_on = true;
		ret = true;
	}

	return ret;
}

static bool skip_telem_region(struct telemetry_region *tr, struct event_group *e)
{
	if (tr->guid != e->guid)
		return true;
	if (tr->plat_info.package_id >= topology_max_packages()) {
		pr_warn("Bad package %u in guid 0x%x\n", tr->plat_info.package_id,
			tr->guid);
		return true;
	}
	if (tr->size != e->mmio_size) {
		pr_warn("MMIO space wrong size (%zu bytes) for guid 0x%x. Expected %zu bytes.\n",
			tr->size, e->guid, e->mmio_size);
		return true;
	}

	return false;
}

static bool group_has_usable_regions(struct event_group *e, struct pmt_feature_group *p)
{
	bool usable_regions = false;

	for (int i = 0; i < p->count; i++) {
		if (skip_telem_region(&p->regions[i], e)) {
			/*
			 * Clear the address field of regions that did not pass the checks in
			 * skip_telem_region() so they will not be used by intel_aet_read_event().
			 * This is safe to do because intel_pmt_get_regions_by_feature() allocates
			 * a new pmt_feature_group structure to return to each caller and only makes
			 * use of the pmt_feature_group::kref field when intel_pmt_put_feature_group()
			 * returns the structure.
			 */
			p->regions[i].addr = NULL;

			continue;
		}
		usable_regions = true;
	}

	return usable_regions;
}

static bool all_regions_have_sufficient_rmid(struct event_group *e, struct pmt_feature_group *p)
{
	struct telemetry_region *tr;

	for (int i = 0; i < p->count; i++) {
		if (!p->regions[i].addr)
			continue;
		tr = &p->regions[i];
		if (tr->num_rmids < e->num_rmid) {
			e->force_off = true;
			return false;
		}
	}

	return true;
}

static bool enable_events(struct event_group *e, struct pmt_feature_group *p)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_PERF_PKG].r_resctrl;
	int skipped_events = 0;

	if (e->force_off)
		return false;

	if (!group_has_usable_regions(e, p))
		return false;

	/*
	 * Only enable event group with insufficient RMIDs if the user requested
	 * it from the kernel command line.
	 */
	if (!all_regions_have_sufficient_rmid(e, p) && !e->force_on) {
		pr_info("%s %s:0x%x monitoring not enabled due to insufficient RMIDs\n",
			r->name, e->pfname, e->guid);
		return false;
	}

	for (int i = 0; i < p->count; i++) {
		if (!p->regions[i].addr)
			continue;
		/*
		 * e->num_rmid only adjusted lower if user (via rdt= kernel
		 * parameter) forces an event group with insufficient RMID
		 * to be enabled.
		 */
		e->num_rmid = min(e->num_rmid, p->regions[i].num_rmids);
	}

	for (int j = 0; j < e->num_events; j++) {
		if (!resctrl_enable_mon_event(e->evts[j].id, true,
					      e->evts[j].bin_bits, &e->evts[j]))
			skipped_events++;
	}
	if (e->num_events == skipped_events) {
		pr_info("No events enabled in %s %s:0x%x\n", r->name, e->pfname, e->guid);
		return false;
	}

	if (r->mon.num_rmid)
		r->mon.num_rmid = min(r->mon.num_rmid, e->num_rmid);
	else
		r->mon.num_rmid = e->num_rmid;

	if (skipped_events)
		pr_info("%s %s:0x%x monitoring detected (skipped %d events)\n", r->name,
			e->pfname, e->guid, skipped_events);
	else
		pr_info("%s %s:0x%x monitoring detected\n", r->name, e->pfname, e->guid);

	return true;
}

static enum pmt_feature_id lookup_pfid(const char *pfname)
{
	if (!strcmp(pfname, "energy"))
		return FEATURE_PER_RMID_ENERGY_TELEM;
	else if (!strcmp(pfname, "perf"))
		return FEATURE_PER_RMID_PERF_TELEM;

	pr_warn("Unknown PMT feature name '%s'\n", pfname);

	return FEATURE_INVALID;
}

/*
 * Request a copy of struct pmt_feature_group for each event group. If there is
 * one, the returned structure has an array of telemetry_region structures,
 * each element of the array describes one telemetry aggregator. The
 * telemetry aggregators may have different GUIDs so obtain duplicate struct
 * pmt_feature_group for event groups with same feature type but different
 * GUID. Post-processing ensures an event group can only use the telemetry
 * aggregators that match its GUID. An event group keeps a pointer to its
 * struct pmt_feature_group to indicate that its events are successfully
 * enabled.
 */
bool intel_aet_get_events(void)
{
	struct pmt_feature_group *p;
	enum pmt_feature_id pfid;
	struct event_group **peg;
	bool ret = false;

	for_each_event_group(peg) {
		pfid = lookup_pfid((*peg)->pfname);
		p = intel_pmt_get_regions_by_feature(pfid);
		if (IS_ERR_OR_NULL(p))
			continue;
		if (enable_events(*peg, p)) {
			(*peg)->pfg = p;
			ret = true;
		} else {
			intel_pmt_put_feature_group(p);
		}
	}

	return ret;
}

void __exit intel_aet_exit(void)
{
	struct event_group **peg;

	for_each_event_group(peg) {
		if ((*peg)->pfg) {
			intel_pmt_put_feature_group((*peg)->pfg);
			(*peg)->pfg = NULL;
		}
	}
}

#define DATA_VALID	BIT_ULL(63)
#define DATA_BITS	GENMASK_ULL(62, 0)

/*
 * Read counter for an event on a domain (summing all aggregators on the
 * domain). If an aggregator hasn't received any data for a specific RMID,
 * the MMIO read indicates that data is not valid.  Return success if at
 * least one aggregator has valid data.
 */
int intel_aet_read_event(int domid, u32 rmid, void *arch_priv, u64 *val)
{
	struct pmt_event *pevt = arch_priv;
	struct event_group *e;
	bool valid = false;
	u64 total = 0;
	u64 evtcount;
	void *pevt0;
	u32 idx;

	pevt0 = pevt - pevt->idx;
	e = container_of(pevt0, struct event_group, evts);
	idx = rmid * e->num_events;
	idx += pevt->idx;

	if (idx * sizeof(u64) + sizeof(u64) > e->mmio_size) {
		pr_warn_once("MMIO index %u out of range\n", idx);
		return -EIO;
	}

	for (int i = 0; i < e->pfg->count; i++) {
		if (!e->pfg->regions[i].addr)
			continue;
		if (e->pfg->regions[i].plat_info.package_id != domid)
			continue;
		evtcount = readq(e->pfg->regions[i].addr + idx * sizeof(u64));
		if (!(evtcount & DATA_VALID))
			continue;
		total += evtcount & DATA_BITS;
		valid = true;
	}

	if (valid)
		*val = total;

	return valid ? 0 : -EINVAL;
}

void intel_aet_mon_domain_setup(int cpu, int id, struct rdt_resource *r,
				struct list_head *add_pos)
{
	struct rdt_perf_pkg_mon_domain *d;
	int err;

	d = kzalloc_node(sizeof(*d), GFP_KERNEL, cpu_to_node(cpu));
	if (!d)
		return;

	d->hdr.id = id;
	d->hdr.type = RESCTRL_MON_DOMAIN;
	d->hdr.rid = RDT_RESOURCE_PERF_PKG;
	cpumask_set_cpu(cpu, &d->hdr.cpu_mask);
	list_add_tail_rcu(&d->hdr.list, add_pos);

	err = resctrl_online_mon_domain(r, &d->hdr);
	if (err) {
		list_del_rcu(&d->hdr.list);
		synchronize_rcu();
		kfree(d);
	}
}
