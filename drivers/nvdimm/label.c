/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/device.h>
#include <linux/ndctl.h>
#include <linux/uuid.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/nd.h>
#include "nd-core.h"
#include "label.h"
#include "nd.h"

static guid_t nvdimm_btt_guid;
static guid_t nvdimm_btt2_guid;
static guid_t nvdimm_pfn_guid;
static guid_t nvdimm_dax_guid;

static u32 best_seq(u32 a, u32 b)
{
	a &= NSINDEX_SEQ_MASK;
	b &= NSINDEX_SEQ_MASK;

	if (a == 0 || a == b)
		return b;
	else if (b == 0)
		return a;
	else if (nd_inc_seq(a) == b)
		return b;
	else
		return a;
}

unsigned sizeof_namespace_label(struct nvdimm_drvdata *ndd)
{
	return ndd->nslabel_size;
}

int nvdimm_num_label_slots(struct nvdimm_drvdata *ndd)
{
	return ndd->nsarea.config_size / (sizeof_namespace_label(ndd) + 1);
}

size_t sizeof_namespace_index(struct nvdimm_drvdata *ndd)
{
	u32 nslot, space, size;

	/*
	 * The minimum index space is 512 bytes, with that amount of
	 * index we can describe ~1400 labels which is less than a byte
	 * of overhead per label.  Round up to a byte of overhead per
	 * label and determine the size of the index region.  Yes, this
	 * starts to waste space at larger config_sizes, but it's
	 * unlikely we'll ever see anything but 128K.
	 */
	nslot = nvdimm_num_label_slots(ndd);
	space = ndd->nsarea.config_size - nslot * sizeof_namespace_label(ndd);
	size = ALIGN(sizeof(struct nd_namespace_index) + DIV_ROUND_UP(nslot, 8),
			NSINDEX_ALIGN) * 2;
	if (size <= space)
		return size / 2;

	dev_err(ndd->dev, "label area (%d) too small to host (%d byte) labels\n",
			ndd->nsarea.config_size, sizeof_namespace_label(ndd));
	return 0;
}

static int __nd_label_validate(struct nvdimm_drvdata *ndd)
{
	/*
	 * On media label format consists of two index blocks followed
	 * by an array of labels.  None of these structures are ever
	 * updated in place.  A sequence number tracks the current
	 * active index and the next one to write, while labels are
	 * written to free slots.
	 *
	 *     +------------+
	 *     |            |
	 *     |  nsindex0  |
	 *     |            |
	 *     +------------+
	 *     |            |
	 *     |  nsindex1  |
	 *     |            |
	 *     +------------+
	 *     |   label0   |
	 *     +------------+
	 *     |   label1   |
	 *     +------------+
	 *     |            |
	 *      ....nslot...
	 *     |            |
	 *     +------------+
	 *     |   labelN   |
	 *     +------------+
	 */
	struct nd_namespace_index *nsindex[] = {
		to_namespace_index(ndd, 0),
		to_namespace_index(ndd, 1),
	};
	const int num_index = ARRAY_SIZE(nsindex);
	struct device *dev = ndd->dev;
	bool valid[2] = { 0 };
	int i, num_valid = 0;
	u32 seq;

	for (i = 0; i < num_index; i++) {
		u32 nslot;
		u8 sig[NSINDEX_SIG_LEN];
		u64 sum_save, sum, size;
		unsigned int version, labelsize;

		memcpy(sig, nsindex[i]->sig, NSINDEX_SIG_LEN);
		if (memcmp(sig, NSINDEX_SIGNATURE, NSINDEX_SIG_LEN) != 0) {
			dev_dbg(dev, "%s: nsindex%d signature invalid\n",
					__func__, i);
			continue;
		}

		/* label sizes larger than 128 arrived with v1.2 */
		version = __le16_to_cpu(nsindex[i]->major) * 100
			+ __le16_to_cpu(nsindex[i]->minor);
		if (version >= 102)
			labelsize = 1 << (7 + nsindex[i]->labelsize);
		else
			labelsize = 128;

		if (labelsize != sizeof_namespace_label(ndd)) {
			dev_dbg(dev, "%s: nsindex%d labelsize %d invalid\n",
					__func__, i, nsindex[i]->labelsize);
			continue;
		}

		sum_save = __le64_to_cpu(nsindex[i]->checksum);
		nsindex[i]->checksum = __cpu_to_le64(0);
		sum = nd_fletcher64(nsindex[i], sizeof_namespace_index(ndd), 1);
		nsindex[i]->checksum = __cpu_to_le64(sum_save);
		if (sum != sum_save) {
			dev_dbg(dev, "%s: nsindex%d checksum invalid\n",
					__func__, i);
			continue;
		}

		seq = __le32_to_cpu(nsindex[i]->seq);
		if ((seq & NSINDEX_SEQ_MASK) == 0) {
			dev_dbg(dev, "%s: nsindex%d sequence: %#x invalid\n",
					__func__, i, seq);
			continue;
		}

		/* sanity check the index against expected values */
		if (__le64_to_cpu(nsindex[i]->myoff)
				!= i * sizeof_namespace_index(ndd)) {
			dev_dbg(dev, "%s: nsindex%d myoff: %#llx invalid\n",
					__func__, i, (unsigned long long)
					__le64_to_cpu(nsindex[i]->myoff));
			continue;
		}
		if (__le64_to_cpu(nsindex[i]->otheroff)
				!= (!i) * sizeof_namespace_index(ndd)) {
			dev_dbg(dev, "%s: nsindex%d otheroff: %#llx invalid\n",
					__func__, i, (unsigned long long)
					__le64_to_cpu(nsindex[i]->otheroff));
			continue;
		}

		size = __le64_to_cpu(nsindex[i]->mysize);
		if (size > sizeof_namespace_index(ndd)
				|| size < sizeof(struct nd_namespace_index)) {
			dev_dbg(dev, "%s: nsindex%d mysize: %#llx invalid\n",
					__func__, i, size);
			continue;
		}

		nslot = __le32_to_cpu(nsindex[i]->nslot);
		if (nslot * sizeof_namespace_label(ndd)
				+ 2 * sizeof_namespace_index(ndd)
				> ndd->nsarea.config_size) {
			dev_dbg(dev, "%s: nsindex%d nslot: %u invalid, config_size: %#x\n",
					__func__, i, nslot,
					ndd->nsarea.config_size);
			continue;
		}
		valid[i] = true;
		num_valid++;
	}

	switch (num_valid) {
	case 0:
		break;
	case 1:
		for (i = 0; i < num_index; i++)
			if (valid[i])
				return i;
		/* can't have num_valid > 0 but valid[] = { false, false } */
		WARN_ON(1);
		break;
	default:
		/* pick the best index... */
		seq = best_seq(__le32_to_cpu(nsindex[0]->seq),
				__le32_to_cpu(nsindex[1]->seq));
		if (seq == (__le32_to_cpu(nsindex[1]->seq) & NSINDEX_SEQ_MASK))
			return 1;
		else
			return 0;
		break;
	}

	return -1;
}

int nd_label_validate(struct nvdimm_drvdata *ndd)
{
	/*
	 * In order to probe for and validate namespace index blocks we
	 * need to know the size of the labels, and we can't trust the
	 * size of the labels until we validate the index blocks.
	 * Resolve this dependency loop by probing for known label
	 * sizes, but default to v1.2 256-byte namespace labels if
	 * discovery fails.
	 */
	int label_size[] = { 128, 256 };
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(label_size); i++) {
		ndd->nslabel_size = label_size[i];
		rc = __nd_label_validate(ndd);
		if (rc >= 0)
			return rc;
	}

	return -1;
}

void nd_label_copy(struct nvdimm_drvdata *ndd, struct nd_namespace_index *dst,
		struct nd_namespace_index *src)
{
	if (dst && src)
		/* pass */;
	else
		return;

	memcpy(dst, src, sizeof_namespace_index(ndd));
}

static struct nd_namespace_label *nd_label_base(struct nvdimm_drvdata *ndd)
{
	void *base = to_namespace_index(ndd, 0);

	return base + 2 * sizeof_namespace_index(ndd);
}

static int to_slot(struct nvdimm_drvdata *ndd,
		struct nd_namespace_label *nd_label)
{
	unsigned long label, base;

	label = (unsigned long) nd_label;
	base = (unsigned long) nd_label_base(ndd);

	return (label - base) / sizeof_namespace_label(ndd);
}

static struct nd_namespace_label *to_label(struct nvdimm_drvdata *ndd, int slot)
{
	unsigned long label, base;

	base = (unsigned long) nd_label_base(ndd);
	label = base + sizeof_namespace_label(ndd) * slot;

	return (struct nd_namespace_label *) label;
}

#define for_each_clear_bit_le(bit, addr, size) \
	for ((bit) = find_next_zero_bit_le((addr), (size), 0);  \
	     (bit) < (size);                                    \
	     (bit) = find_next_zero_bit_le((addr), (size), (bit) + 1))

/**
 * preamble_index - common variable initialization for nd_label_* routines
 * @ndd: dimm container for the relevant label set
 * @idx: namespace_index index
 * @nsindex_out: on return set to the currently active namespace index
 * @free: on return set to the free label bitmap in the index
 * @nslot: on return set to the number of slots in the label space
 */
static bool preamble_index(struct nvdimm_drvdata *ndd, int idx,
		struct nd_namespace_index **nsindex_out,
		unsigned long **free, u32 *nslot)
{
	struct nd_namespace_index *nsindex;

	nsindex = to_namespace_index(ndd, idx);
	if (nsindex == NULL)
		return false;

	*free = (unsigned long *) nsindex->free;
	*nslot = __le32_to_cpu(nsindex->nslot);
	*nsindex_out = nsindex;

	return true;
}

char *nd_label_gen_id(struct nd_label_id *label_id, u8 *uuid, u32 flags)
{
	if (!label_id || !uuid)
		return NULL;
	snprintf(label_id->id, ND_LABEL_ID_SIZE, "%s-%pUb",
			flags & NSLABEL_FLAG_LOCAL ? "blk" : "pmem", uuid);
	return label_id->id;
}

static bool preamble_current(struct nvdimm_drvdata *ndd,
		struct nd_namespace_index **nsindex,
		unsigned long **free, u32 *nslot)
{
	return preamble_index(ndd, ndd->ns_current, nsindex,
			free, nslot);
}

static bool preamble_next(struct nvdimm_drvdata *ndd,
		struct nd_namespace_index **nsindex,
		unsigned long **free, u32 *nslot)
{
	return preamble_index(ndd, ndd->ns_next, nsindex,
			free, nslot);
}

static bool slot_valid(struct nvdimm_drvdata *ndd,
		struct nd_namespace_label *nd_label, u32 slot)
{
	/* check that we are written where we expect to be written */
	if (slot != __le32_to_cpu(nd_label->slot))
		return false;

	/* check that DPA allocations are page aligned */
	if ((__le64_to_cpu(nd_label->dpa)
				| __le64_to_cpu(nd_label->rawsize)) % SZ_4K)
		return false;

	/* check checksum */
	if (namespace_label_has(ndd, checksum)) {
		u64 sum, sum_save;

		sum_save = __le64_to_cpu(nd_label->checksum);
		nd_label->checksum = __cpu_to_le64(0);
		sum = nd_fletcher64(nd_label, sizeof_namespace_label(ndd), 1);
		nd_label->checksum = __cpu_to_le64(sum_save);
		if (sum != sum_save) {
			dev_dbg(ndd->dev, "%s fail checksum. slot: %d expect: %#llx\n",
				__func__, slot, sum);
			return false;
		}
	}

	return true;
}

int nd_label_reserve_dpa(struct nvdimm_drvdata *ndd)
{
	struct nd_namespace_index *nsindex;
	unsigned long *free;
	u32 nslot, slot;

	if (!preamble_current(ndd, &nsindex, &free, &nslot))
		return 0; /* no label, nothing to reserve */

	for_each_clear_bit_le(slot, free, nslot) {
		struct nd_namespace_label *nd_label;
		struct nd_region *nd_region = NULL;
		u8 label_uuid[NSLABEL_UUID_LEN];
		struct nd_label_id label_id;
		struct resource *res;
		u32 flags;

		nd_label = to_label(ndd, slot);

		if (!slot_valid(ndd, nd_label, slot))
			continue;

		memcpy(label_uuid, nd_label->uuid, NSLABEL_UUID_LEN);
		flags = __le32_to_cpu(nd_label->flags);
		nd_label_gen_id(&label_id, label_uuid, flags);
		res = nvdimm_allocate_dpa(ndd, &label_id,
				__le64_to_cpu(nd_label->dpa),
				__le64_to_cpu(nd_label->rawsize));
		nd_dbg_dpa(nd_region, ndd, res, "reserve\n");
		if (!res)
			return -EBUSY;
	}

	return 0;
}

int nd_label_active_count(struct nvdimm_drvdata *ndd)
{
	struct nd_namespace_index *nsindex;
	unsigned long *free;
	u32 nslot, slot;
	int count = 0;

	if (!preamble_current(ndd, &nsindex, &free, &nslot))
		return 0;

	for_each_clear_bit_le(slot, free, nslot) {
		struct nd_namespace_label *nd_label;

		nd_label = to_label(ndd, slot);

		if (!slot_valid(ndd, nd_label, slot)) {
			u32 label_slot = __le32_to_cpu(nd_label->slot);
			u64 size = __le64_to_cpu(nd_label->rawsize);
			u64 dpa = __le64_to_cpu(nd_label->dpa);

			dev_dbg(ndd->dev,
				"%s: slot%d invalid slot: %d dpa: %llx size: %llx\n",
					__func__, slot, label_slot, dpa, size);
			continue;
		}
		count++;
	}
	return count;
}

struct nd_namespace_label *nd_label_active(struct nvdimm_drvdata *ndd, int n)
{
	struct nd_namespace_index *nsindex;
	unsigned long *free;
	u32 nslot, slot;

	if (!preamble_current(ndd, &nsindex, &free, &nslot))
		return NULL;

	for_each_clear_bit_le(slot, free, nslot) {
		struct nd_namespace_label *nd_label;

		nd_label = to_label(ndd, slot);
		if (!slot_valid(ndd, nd_label, slot))
			continue;

		if (n-- == 0)
			return to_label(ndd, slot);
	}

	return NULL;
}

u32 nd_label_alloc_slot(struct nvdimm_drvdata *ndd)
{
	struct nd_namespace_index *nsindex;
	unsigned long *free;
	u32 nslot, slot;

	if (!preamble_next(ndd, &nsindex, &free, &nslot))
		return UINT_MAX;

	WARN_ON(!is_nvdimm_bus_locked(ndd->dev));

	slot = find_next_bit_le(free, nslot, 0);
	if (slot == nslot)
		return UINT_MAX;

	clear_bit_le(slot, free);

	return slot;
}

bool nd_label_free_slot(struct nvdimm_drvdata *ndd, u32 slot)
{
	struct nd_namespace_index *nsindex;
	unsigned long *free;
	u32 nslot;

	if (!preamble_next(ndd, &nsindex, &free, &nslot))
		return false;

	WARN_ON(!is_nvdimm_bus_locked(ndd->dev));

	if (slot < nslot)
		return !test_and_set_bit_le(slot, free);
	return false;
}

u32 nd_label_nfree(struct nvdimm_drvdata *ndd)
{
	struct nd_namespace_index *nsindex;
	unsigned long *free;
	u32 nslot;

	WARN_ON(!is_nvdimm_bus_locked(ndd->dev));

	if (!preamble_next(ndd, &nsindex, &free, &nslot))
		return nvdimm_num_label_slots(ndd);

	return bitmap_weight(free, nslot);
}

static int nd_label_write_index(struct nvdimm_drvdata *ndd, int index, u32 seq,
		unsigned long flags)
{
	struct nd_namespace_index *nsindex;
	unsigned long offset;
	u64 checksum;
	u32 nslot;
	int rc;

	nsindex = to_namespace_index(ndd, index);
	if (flags & ND_NSINDEX_INIT)
		nslot = nvdimm_num_label_slots(ndd);
	else
		nslot = __le32_to_cpu(nsindex->nslot);

	memcpy(nsindex->sig, NSINDEX_SIGNATURE, NSINDEX_SIG_LEN);
	memset(&nsindex->flags, 0, 3);
	nsindex->labelsize = sizeof_namespace_label(ndd) >> 8;
	nsindex->seq = __cpu_to_le32(seq);
	offset = (unsigned long) nsindex
		- (unsigned long) to_namespace_index(ndd, 0);
	nsindex->myoff = __cpu_to_le64(offset);
	nsindex->mysize = __cpu_to_le64(sizeof_namespace_index(ndd));
	offset = (unsigned long) to_namespace_index(ndd,
			nd_label_next_nsindex(index))
		- (unsigned long) to_namespace_index(ndd, 0);
	nsindex->otheroff = __cpu_to_le64(offset);
	offset = (unsigned long) nd_label_base(ndd)
		- (unsigned long) to_namespace_index(ndd, 0);
	nsindex->labeloff = __cpu_to_le64(offset);
	nsindex->nslot = __cpu_to_le32(nslot);
	nsindex->major = __cpu_to_le16(1);
	if (sizeof_namespace_label(ndd) < 256)
		nsindex->minor = __cpu_to_le16(1);
	else
		nsindex->minor = __cpu_to_le16(2);
	nsindex->checksum = __cpu_to_le64(0);
	if (flags & ND_NSINDEX_INIT) {
		unsigned long *free = (unsigned long *) nsindex->free;
		u32 nfree = ALIGN(nslot, BITS_PER_LONG);
		int last_bits, i;

		memset(nsindex->free, 0xff, nfree / 8);
		for (i = 0, last_bits = nfree - nslot; i < last_bits; i++)
			clear_bit_le(nslot + i, free);
	}
	checksum = nd_fletcher64(nsindex, sizeof_namespace_index(ndd), 1);
	nsindex->checksum = __cpu_to_le64(checksum);
	rc = nvdimm_set_config_data(ndd, __le64_to_cpu(nsindex->myoff),
			nsindex, sizeof_namespace_index(ndd));
	if (rc < 0)
		return rc;

	if (flags & ND_NSINDEX_INIT)
		return 0;

	/* copy the index we just wrote to the new 'next' */
	WARN_ON(index != ndd->ns_next);
	nd_label_copy(ndd, to_current_namespace_index(ndd), nsindex);
	ndd->ns_current = nd_label_next_nsindex(ndd->ns_current);
	ndd->ns_next = nd_label_next_nsindex(ndd->ns_next);
	WARN_ON(ndd->ns_current == ndd->ns_next);

	return 0;
}

static unsigned long nd_label_offset(struct nvdimm_drvdata *ndd,
		struct nd_namespace_label *nd_label)
{
	return (unsigned long) nd_label
		- (unsigned long) to_namespace_index(ndd, 0);
}

enum nvdimm_claim_class to_nvdimm_cclass(guid_t *guid)
{
	if (guid_equal(guid, &nvdimm_btt_guid))
		return NVDIMM_CCLASS_BTT;
	else if (guid_equal(guid, &nvdimm_btt2_guid))
		return NVDIMM_CCLASS_BTT2;
	else if (guid_equal(guid, &nvdimm_pfn_guid))
		return NVDIMM_CCLASS_PFN;
	else if (guid_equal(guid, &nvdimm_dax_guid))
		return NVDIMM_CCLASS_DAX;
	else if (guid_equal(guid, &guid_null))
		return NVDIMM_CCLASS_NONE;

	return NVDIMM_CCLASS_UNKNOWN;
}

static const guid_t *to_abstraction_guid(enum nvdimm_claim_class claim_class,
	guid_t *target)
{
	if (claim_class == NVDIMM_CCLASS_BTT)
		return &nvdimm_btt_guid;
	else if (claim_class == NVDIMM_CCLASS_BTT2)
		return &nvdimm_btt2_guid;
	else if (claim_class == NVDIMM_CCLASS_PFN)
		return &nvdimm_pfn_guid;
	else if (claim_class == NVDIMM_CCLASS_DAX)
		return &nvdimm_dax_guid;
	else if (claim_class == NVDIMM_CCLASS_UNKNOWN) {
		/*
		 * If we're modifying a namespace for which we don't
		 * know the claim_class, don't touch the existing guid.
		 */
		return target;
	} else
		return &guid_null;
}

static int __pmem_label_update(struct nd_region *nd_region,
		struct nd_mapping *nd_mapping, struct nd_namespace_pmem *nspm,
		int pos)
{
	struct nd_namespace_common *ndns = &nspm->nsio.common;
	struct nd_interleave_set *nd_set = nd_region->nd_set;
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nd_label_ent *label_ent, *victim = NULL;
	struct nd_namespace_label *nd_label;
	struct nd_namespace_index *nsindex;
	struct nd_label_id label_id;
	struct resource *res;
	unsigned long *free;
	u32 nslot, slot;
	size_t offset;
	u64 cookie;
	int rc;

	if (!preamble_next(ndd, &nsindex, &free, &nslot))
		return -ENXIO;

	cookie = nd_region_interleave_set_cookie(nd_region, nsindex);
	nd_label_gen_id(&label_id, nspm->uuid, 0);
	for_each_dpa_resource(ndd, res)
		if (strcmp(res->name, label_id.id) == 0)
			break;

	if (!res) {
		WARN_ON_ONCE(1);
		return -ENXIO;
	}

	/* allocate and write the label to the staging (next) index */
	slot = nd_label_alloc_slot(ndd);
	if (slot == UINT_MAX)
		return -ENXIO;
	dev_dbg(ndd->dev, "%s: allocated: %d\n", __func__, slot);

	nd_label = to_label(ndd, slot);
	memset(nd_label, 0, sizeof_namespace_label(ndd));
	memcpy(nd_label->uuid, nspm->uuid, NSLABEL_UUID_LEN);
	if (nspm->alt_name)
		memcpy(nd_label->name, nspm->alt_name, NSLABEL_NAME_LEN);
	nd_label->flags = __cpu_to_le32(NSLABEL_FLAG_UPDATING);
	nd_label->nlabel = __cpu_to_le16(nd_region->ndr_mappings);
	nd_label->position = __cpu_to_le16(pos);
	nd_label->isetcookie = __cpu_to_le64(cookie);
	nd_label->rawsize = __cpu_to_le64(resource_size(res));
	nd_label->lbasize = __cpu_to_le64(nspm->lbasize);
	nd_label->dpa = __cpu_to_le64(res->start);
	nd_label->slot = __cpu_to_le32(slot);
	if (namespace_label_has(ndd, type_guid))
		guid_copy(&nd_label->type_guid, &nd_set->type_guid);
	if (namespace_label_has(ndd, abstraction_guid))
		guid_copy(&nd_label->abstraction_guid,
				to_abstraction_guid(ndns->claim_class,
					&nd_label->abstraction_guid));
	if (namespace_label_has(ndd, checksum)) {
		u64 sum;

		nd_label->checksum = __cpu_to_le64(0);
		sum = nd_fletcher64(nd_label, sizeof_namespace_label(ndd), 1);
		nd_label->checksum = __cpu_to_le64(sum);
	}
	nd_dbg_dpa(nd_region, ndd, res, "%s\n", __func__);

	/* update label */
	offset = nd_label_offset(ndd, nd_label);
	rc = nvdimm_set_config_data(ndd, offset, nd_label,
			sizeof_namespace_label(ndd));
	if (rc < 0)
		return rc;

	/* Garbage collect the previous label */
	mutex_lock(&nd_mapping->lock);
	list_for_each_entry(label_ent, &nd_mapping->labels, list) {
		if (!label_ent->label)
			continue;
		if (memcmp(nspm->uuid, label_ent->label->uuid,
					NSLABEL_UUID_LEN) != 0)
			continue;
		victim = label_ent;
		list_move_tail(&victim->list, &nd_mapping->labels);
		break;
	}
	if (victim) {
		dev_dbg(ndd->dev, "%s: free: %d\n", __func__, slot);
		slot = to_slot(ndd, victim->label);
		nd_label_free_slot(ndd, slot);
		victim->label = NULL;
	}

	/* update index */
	rc = nd_label_write_index(ndd, ndd->ns_next,
			nd_inc_seq(__le32_to_cpu(nsindex->seq)), 0);
	if (rc == 0) {
		list_for_each_entry(label_ent, &nd_mapping->labels, list)
			if (!label_ent->label) {
				label_ent->label = nd_label;
				nd_label = NULL;
				break;
			}
		dev_WARN_ONCE(&nspm->nsio.common.dev, nd_label,
				"failed to track label: %d\n",
				to_slot(ndd, nd_label));
		if (nd_label)
			rc = -ENXIO;
	}
	mutex_unlock(&nd_mapping->lock);

	return rc;
}

static bool is_old_resource(struct resource *res, struct resource **list, int n)
{
	int i;

	if (res->flags & DPA_RESOURCE_ADJUSTED)
		return false;
	for (i = 0; i < n; i++)
		if (res == list[i])
			return true;
	return false;
}

static struct resource *to_resource(struct nvdimm_drvdata *ndd,
		struct nd_namespace_label *nd_label)
{
	struct resource *res;

	for_each_dpa_resource(ndd, res) {
		if (res->start != __le64_to_cpu(nd_label->dpa))
			continue;
		if (resource_size(res) != __le64_to_cpu(nd_label->rawsize))
			continue;
		return res;
	}

	return NULL;
}

/*
 * 1/ Account all the labels that can be freed after this update
 * 2/ Allocate and write the label to the staging (next) index
 * 3/ Record the resources in the namespace device
 */
static int __blk_label_update(struct nd_region *nd_region,
		struct nd_mapping *nd_mapping, struct nd_namespace_blk *nsblk,
		int num_labels)
{
	int i, alloc, victims, nfree, old_num_resources, nlabel, rc = -ENXIO;
	struct nd_interleave_set *nd_set = nd_region->nd_set;
	struct nd_namespace_common *ndns = &nsblk->common;
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nd_namespace_label *nd_label;
	struct nd_label_ent *label_ent, *e;
	struct nd_namespace_index *nsindex;
	unsigned long *free, *victim_map = NULL;
	struct resource *res, **old_res_list;
	struct nd_label_id label_id;
	u8 uuid[NSLABEL_UUID_LEN];
	int min_dpa_idx = 0;
	LIST_HEAD(list);
	u32 nslot, slot;

	if (!preamble_next(ndd, &nsindex, &free, &nslot))
		return -ENXIO;

	old_res_list = nsblk->res;
	nfree = nd_label_nfree(ndd);
	old_num_resources = nsblk->num_resources;
	nd_label_gen_id(&label_id, nsblk->uuid, NSLABEL_FLAG_LOCAL);

	/*
	 * We need to loop over the old resources a few times, which seems a
	 * bit inefficient, but we need to know that we have the label
	 * space before we start mutating the tracking structures.
	 * Otherwise the recovery method of last resort for userspace is
	 * disable and re-enable the parent region.
	 */
	alloc = 0;
	for_each_dpa_resource(ndd, res) {
		if (strcmp(res->name, label_id.id) != 0)
			continue;
		if (!is_old_resource(res, old_res_list, old_num_resources))
			alloc++;
	}

	victims = 0;
	if (old_num_resources) {
		/* convert old local-label-map to dimm-slot victim-map */
		victim_map = kcalloc(BITS_TO_LONGS(nslot), sizeof(long),
				GFP_KERNEL);
		if (!victim_map)
			return -ENOMEM;

		/* mark unused labels for garbage collection */
		for_each_clear_bit_le(slot, free, nslot) {
			nd_label = to_label(ndd, slot);
			memcpy(uuid, nd_label->uuid, NSLABEL_UUID_LEN);
			if (memcmp(uuid, nsblk->uuid, NSLABEL_UUID_LEN) != 0)
				continue;
			res = to_resource(ndd, nd_label);
			if (res && is_old_resource(res, old_res_list,
						old_num_resources))
				continue;
			slot = to_slot(ndd, nd_label);
			set_bit(slot, victim_map);
			victims++;
		}
	}

	/* don't allow updates that consume the last label */
	if (nfree - alloc < 0 || nfree - alloc + victims < 1) {
		dev_info(&nsblk->common.dev, "insufficient label space\n");
		kfree(victim_map);
		return -ENOSPC;
	}
	/* from here on we need to abort on error */


	/* assign all resources to the namespace before writing the labels */
	nsblk->res = NULL;
	nsblk->num_resources = 0;
	for_each_dpa_resource(ndd, res) {
		if (strcmp(res->name, label_id.id) != 0)
			continue;
		if (!nsblk_add_resource(nd_region, ndd, nsblk, res->start)) {
			rc = -ENOMEM;
			goto abort;
		}
	}

	/*
	 * Find the resource associated with the first label in the set
	 * per the v1.2 namespace specification.
	 */
	for (i = 0; i < nsblk->num_resources; i++) {
		struct resource *min = nsblk->res[min_dpa_idx];

		res = nsblk->res[i];
		if (res->start < min->start)
			min_dpa_idx = i;
	}

	for (i = 0; i < nsblk->num_resources; i++) {
		size_t offset;

		res = nsblk->res[i];
		if (is_old_resource(res, old_res_list, old_num_resources))
			continue; /* carry-over */
		slot = nd_label_alloc_slot(ndd);
		if (slot == UINT_MAX)
			goto abort;
		dev_dbg(ndd->dev, "%s: allocated: %d\n", __func__, slot);

		nd_label = to_label(ndd, slot);
		memset(nd_label, 0, sizeof_namespace_label(ndd));
		memcpy(nd_label->uuid, nsblk->uuid, NSLABEL_UUID_LEN);
		if (nsblk->alt_name)
			memcpy(nd_label->name, nsblk->alt_name,
					NSLABEL_NAME_LEN);
		nd_label->flags = __cpu_to_le32(NSLABEL_FLAG_LOCAL);

		/*
		 * Use the presence of the type_guid as a flag to
		 * determine isetcookie usage and nlabel + position
		 * policy for blk-aperture namespaces.
		 */
		if (namespace_label_has(ndd, type_guid)) {
			if (i == min_dpa_idx) {
				nd_label->nlabel = __cpu_to_le16(nsblk->num_resources);
				nd_label->position = __cpu_to_le16(0);
			} else {
				nd_label->nlabel = __cpu_to_le16(0xffff);
				nd_label->position = __cpu_to_le16(0xffff);
			}
			nd_label->isetcookie = __cpu_to_le64(nd_set->cookie2);
		} else {
			nd_label->nlabel = __cpu_to_le16(0); /* N/A */
			nd_label->position = __cpu_to_le16(0); /* N/A */
			nd_label->isetcookie = __cpu_to_le64(0); /* N/A */
		}

		nd_label->dpa = __cpu_to_le64(res->start);
		nd_label->rawsize = __cpu_to_le64(resource_size(res));
		nd_label->lbasize = __cpu_to_le64(nsblk->lbasize);
		nd_label->slot = __cpu_to_le32(slot);
		if (namespace_label_has(ndd, type_guid))
			guid_copy(&nd_label->type_guid, &nd_set->type_guid);
		if (namespace_label_has(ndd, abstraction_guid))
			guid_copy(&nd_label->abstraction_guid,
					to_abstraction_guid(ndns->claim_class,
						&nd_label->abstraction_guid));

		if (namespace_label_has(ndd, checksum)) {
			u64 sum;

			nd_label->checksum = __cpu_to_le64(0);
			sum = nd_fletcher64(nd_label,
					sizeof_namespace_label(ndd), 1);
			nd_label->checksum = __cpu_to_le64(sum);
		}

		/* update label */
		offset = nd_label_offset(ndd, nd_label);
		rc = nvdimm_set_config_data(ndd, offset, nd_label,
				sizeof_namespace_label(ndd));
		if (rc < 0)
			goto abort;
	}

	/* free up now unused slots in the new index */
	for_each_set_bit(slot, victim_map, victim_map ? nslot : 0) {
		dev_dbg(ndd->dev, "%s: free: %d\n", __func__, slot);
		nd_label_free_slot(ndd, slot);
	}

	/* update index */
	rc = nd_label_write_index(ndd, ndd->ns_next,
			nd_inc_seq(__le32_to_cpu(nsindex->seq)), 0);
	if (rc)
		goto abort;

	/*
	 * Now that the on-dimm labels are up to date, fix up the tracking
	 * entries in nd_mapping->labels
	 */
	nlabel = 0;
	mutex_lock(&nd_mapping->lock);
	list_for_each_entry_safe(label_ent, e, &nd_mapping->labels, list) {
		nd_label = label_ent->label;
		if (!nd_label)
			continue;
		nlabel++;
		memcpy(uuid, nd_label->uuid, NSLABEL_UUID_LEN);
		if (memcmp(uuid, nsblk->uuid, NSLABEL_UUID_LEN) != 0)
			continue;
		nlabel--;
		list_move(&label_ent->list, &list);
		label_ent->label = NULL;
	}
	list_splice_tail_init(&list, &nd_mapping->labels);
	mutex_unlock(&nd_mapping->lock);

	if (nlabel + nsblk->num_resources > num_labels) {
		/*
		 * Bug, we can't end up with more resources than
		 * available labels
		 */
		WARN_ON_ONCE(1);
		rc = -ENXIO;
		goto out;
	}

	mutex_lock(&nd_mapping->lock);
	label_ent = list_first_entry_or_null(&nd_mapping->labels,
			typeof(*label_ent), list);
	if (!label_ent) {
		WARN_ON(1);
		mutex_unlock(&nd_mapping->lock);
		rc = -ENXIO;
		goto out;
	}
	for_each_clear_bit_le(slot, free, nslot) {
		nd_label = to_label(ndd, slot);
		memcpy(uuid, nd_label->uuid, NSLABEL_UUID_LEN);
		if (memcmp(uuid, nsblk->uuid, NSLABEL_UUID_LEN) != 0)
			continue;
		res = to_resource(ndd, nd_label);
		res->flags &= ~DPA_RESOURCE_ADJUSTED;
		dev_vdbg(&nsblk->common.dev, "assign label slot: %d\n", slot);
		list_for_each_entry_from(label_ent, &nd_mapping->labels, list) {
			if (label_ent->label)
				continue;
			label_ent->label = nd_label;
			nd_label = NULL;
			break;
		}
		if (nd_label)
			dev_WARN(&nsblk->common.dev,
					"failed to track label slot%d\n", slot);
	}
	mutex_unlock(&nd_mapping->lock);

 out:
	kfree(old_res_list);
	kfree(victim_map);
	return rc;

 abort:
	/*
	 * 1/ repair the allocated label bitmap in the index
	 * 2/ restore the resource list
	 */
	nd_label_copy(ndd, nsindex, to_current_namespace_index(ndd));
	kfree(nsblk->res);
	nsblk->res = old_res_list;
	nsblk->num_resources = old_num_resources;
	old_res_list = NULL;
	goto out;
}

static int init_labels(struct nd_mapping *nd_mapping, int num_labels)
{
	int i, old_num_labels = 0;
	struct nd_label_ent *label_ent;
	struct nd_namespace_index *nsindex;
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);

	mutex_lock(&nd_mapping->lock);
	list_for_each_entry(label_ent, &nd_mapping->labels, list)
		old_num_labels++;
	mutex_unlock(&nd_mapping->lock);

	/*
	 * We need to preserve all the old labels for the mapping so
	 * they can be garbage collected after writing the new labels.
	 */
	for (i = old_num_labels; i < num_labels; i++) {
		label_ent = kzalloc(sizeof(*label_ent), GFP_KERNEL);
		if (!label_ent)
			return -ENOMEM;
		mutex_lock(&nd_mapping->lock);
		list_add_tail(&label_ent->list, &nd_mapping->labels);
		mutex_unlock(&nd_mapping->lock);
	}

	if (ndd->ns_current == -1 || ndd->ns_next == -1)
		/* pass */;
	else
		return max(num_labels, old_num_labels);

	nsindex = to_namespace_index(ndd, 0);
	memset(nsindex, 0, ndd->nsarea.config_size);
	for (i = 0; i < 2; i++) {
		int rc = nd_label_write_index(ndd, i, 3 - i, ND_NSINDEX_INIT);

		if (rc)
			return rc;
	}
	ndd->ns_next = 1;
	ndd->ns_current = 0;

	return max(num_labels, old_num_labels);
}

static int del_labels(struct nd_mapping *nd_mapping, u8 *uuid)
{
	struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
	struct nd_label_ent *label_ent, *e;
	struct nd_namespace_index *nsindex;
	u8 label_uuid[NSLABEL_UUID_LEN];
	unsigned long *free;
	LIST_HEAD(list);
	u32 nslot, slot;
	int active = 0;

	if (!uuid)
		return 0;

	/* no index || no labels == nothing to delete */
	if (!preamble_next(ndd, &nsindex, &free, &nslot))
		return 0;

	mutex_lock(&nd_mapping->lock);
	list_for_each_entry_safe(label_ent, e, &nd_mapping->labels, list) {
		struct nd_namespace_label *nd_label = label_ent->label;

		if (!nd_label)
			continue;
		active++;
		memcpy(label_uuid, nd_label->uuid, NSLABEL_UUID_LEN);
		if (memcmp(label_uuid, uuid, NSLABEL_UUID_LEN) != 0)
			continue;
		active--;
		slot = to_slot(ndd, nd_label);
		nd_label_free_slot(ndd, slot);
		dev_dbg(ndd->dev, "%s: free: %d\n", __func__, slot);
		list_move_tail(&label_ent->list, &list);
		label_ent->label = NULL;
	}
	list_splice_tail_init(&list, &nd_mapping->labels);

	if (active == 0) {
		nd_mapping_free_labels(nd_mapping);
		dev_dbg(ndd->dev, "%s: no more active labels\n", __func__);
	}
	mutex_unlock(&nd_mapping->lock);

	return nd_label_write_index(ndd, ndd->ns_next,
			nd_inc_seq(__le32_to_cpu(nsindex->seq)), 0);
}

int nd_pmem_namespace_label_update(struct nd_region *nd_region,
		struct nd_namespace_pmem *nspm, resource_size_t size)
{
	int i;

	for (i = 0; i < nd_region->ndr_mappings; i++) {
		struct nd_mapping *nd_mapping = &nd_region->mapping[i];
		struct nvdimm_drvdata *ndd = to_ndd(nd_mapping);
		struct resource *res;
		int rc, count = 0;

		if (size == 0) {
			rc = del_labels(nd_mapping, nspm->uuid);
			if (rc)
				return rc;
			continue;
		}

		for_each_dpa_resource(ndd, res)
			if (strncmp(res->name, "pmem", 4) == 0)
				count++;
		WARN_ON_ONCE(!count);

		rc = init_labels(nd_mapping, count);
		if (rc < 0)
			return rc;

		rc = __pmem_label_update(nd_region, nd_mapping, nspm, i);
		if (rc)
			return rc;
	}

	return 0;
}

int nd_blk_namespace_label_update(struct nd_region *nd_region,
		struct nd_namespace_blk *nsblk, resource_size_t size)
{
	struct nd_mapping *nd_mapping = &nd_region->mapping[0];
	struct resource *res;
	int count = 0;

	if (size == 0)
		return del_labels(nd_mapping, nsblk->uuid);

	for_each_dpa_resource(to_ndd(nd_mapping), res)
		count++;

	count = init_labels(nd_mapping, count);
	if (count < 0)
		return count;

	return __blk_label_update(nd_region, nd_mapping, nsblk, count);
}

int __init nd_label_init(void)
{
	WARN_ON(guid_parse(NVDIMM_BTT_GUID, &nvdimm_btt_guid));
	WARN_ON(guid_parse(NVDIMM_BTT2_GUID, &nvdimm_btt2_guid));
	WARN_ON(guid_parse(NVDIMM_PFN_GUID, &nvdimm_pfn_guid));
	WARN_ON(guid_parse(NVDIMM_DAX_GUID, &nvdimm_dax_guid));

	return 0;
}
