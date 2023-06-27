// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */

#include <cxl.h>
#include "core.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

static bool cxl_is_hpa_in_range(u64 hpa, struct cxl_region *cxlr, int pos)
{
	struct cxl_region_params *p = &cxlr->params;
	int gran = p->interleave_granularity;
	int ways = p->interleave_ways;
	u64 offset;

	/* Is the hpa within this region at all */
	if (hpa < p->res->start || hpa > p->res->end) {
		dev_dbg(&cxlr->dev,
			"Addr trans fail: hpa 0x%llx not in region\n", hpa);
		return false;
	}

	/* Is the hpa in an expected chunk for its pos(-ition) */
	offset = hpa - p->res->start;
	offset = do_div(offset, gran * ways);
	if ((offset >= pos * gran) && (offset < (pos + 1) * gran))
		return true;

	dev_dbg(&cxlr->dev,
		"Addr trans fail: hpa 0x%llx not in expected chunk\n", hpa);

	return false;
}

static u64 cxl_dpa_to_hpa(u64 dpa,  struct cxl_region *cxlr,
			  struct cxl_endpoint_decoder *cxled)
{
	u64 dpa_offset, hpa_offset, bits_upper, mask_upper, hpa;
	struct cxl_region_params *p = &cxlr->params;
	int pos = cxled->pos;
	u16 eig = 0;
	u8 eiw = 0;

	ways_to_eiw(p->interleave_ways, &eiw);
	granularity_to_eig(p->interleave_granularity, &eig);

	/*
	 * The device position in the region interleave set was removed
	 * from the offset at HPA->DPA translation. To reconstruct the
	 * HPA, place the 'pos' in the offset.
	 *
	 * The placement of 'pos' in the HPA is determined by interleave
	 * ways and granularity and is defined in the CXL Spec 3.0 Section
	 * 8.2.4.19.13 Implementation Note: Device Decode Logic
	 */

	/* Remove the dpa base */
	dpa_offset = dpa - cxl_dpa_resource_start(cxled);

	mask_upper = GENMASK_ULL(51, eig + 8);

	if (eiw < 8) {
		hpa_offset = (dpa_offset & mask_upper) << eiw;
		hpa_offset |= pos << (eig + 8);
	} else {
		bits_upper = (dpa_offset & mask_upper) >> (eig + 8);
		bits_upper = bits_upper * 3;
		hpa_offset = ((bits_upper << (eiw - 8)) + pos) << (eig + 8);
	}

	/* The lower bits remain unchanged */
	hpa_offset |= dpa_offset & GENMASK_ULL(eig + 7, 0);

	/* Apply the hpa_offset to the region base address */
	hpa = hpa_offset + p->res->start;

	if (!cxl_is_hpa_in_range(hpa, cxlr, cxled->pos))
		return ULLONG_MAX;

	return hpa;
}

u64 cxl_trace_hpa(struct cxl_region *cxlr, struct cxl_memdev *cxlmd,
		  u64 dpa)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_endpoint_decoder *cxled = NULL;

	for (int i = 0; i <  p->nr_targets; i++) {
		cxled = p->targets[i];
		if (cxlmd == cxled_to_memdev(cxled))
			break;
	}
	if (!cxled || cxlmd != cxled_to_memdev(cxled))
		return ULLONG_MAX;

	return cxl_dpa_to_hpa(dpa, cxlr, cxled);
}
