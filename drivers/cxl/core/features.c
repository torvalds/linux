// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <cxl/mailbox.h>
#include <cxl/features.h>
#include "cxl.h"
#include "core.h"
#include "cxlmem.h"

inline struct cxl_features_state *to_cxlfs(struct cxl_dev_state *cxlds)
{
	return cxlds->cxlfs;
}
EXPORT_SYMBOL_NS_GPL(to_cxlfs, "CXL");

static int cxl_get_supported_features_count(struct cxl_mailbox *cxl_mbox)
{
	struct cxl_mbox_get_sup_feats_out mbox_out;
	struct cxl_mbox_get_sup_feats_in mbox_in;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	memset(&mbox_in, 0, sizeof(mbox_in));
	mbox_in.count = cpu_to_le32(sizeof(mbox_out));
	memset(&mbox_out, 0, sizeof(mbox_out));
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_SUPPORTED_FEATURES,
		.size_in = sizeof(mbox_in),
		.payload_in = &mbox_in,
		.size_out = sizeof(mbox_out),
		.payload_out = &mbox_out,
		.min_out = sizeof(mbox_out),
	};
	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0)
		return rc;

	return le16_to_cpu(mbox_out.supported_feats);
}

static struct cxl_feat_entries *
get_supported_features(struct cxl_features_state *cxlfs)
{
	int remain_feats, max_size, max_feats, start, rc, hdr_size;
	struct cxl_mailbox *cxl_mbox = &cxlfs->cxlds->cxl_mbox;
	int feat_size = sizeof(struct cxl_feat_entry);
	struct cxl_mbox_get_sup_feats_in mbox_in;
	struct cxl_feat_entry *entry;
	struct cxl_mbox_cmd mbox_cmd;
	int count;

	count = cxl_get_supported_features_count(cxl_mbox);
	if (count <= 0)
		return NULL;

	struct cxl_feat_entries *entries __free(kvfree) =
		kvmalloc(struct_size(entries, ent, count), GFP_KERNEL);
	if (!entries)
		return NULL;

	struct cxl_mbox_get_sup_feats_out *mbox_out __free(kvfree) =
		kvmalloc(cxl_mbox->payload_size, GFP_KERNEL);
	if (!mbox_out)
		return NULL;

	hdr_size = struct_size(mbox_out, ents, 0);
	max_size = cxl_mbox->payload_size - hdr_size;
	/* max feat entries that can fit in mailbox max payload size */
	max_feats = max_size / feat_size;
	entry = entries->ent;

	start = 0;
	remain_feats = count;
	do {
		int retrieved, alloc_size, copy_feats;
		int num_entries;

		if (remain_feats > max_feats) {
			alloc_size = struct_size(mbox_out, ents, max_feats);
			remain_feats = remain_feats - max_feats;
			copy_feats = max_feats;
		} else {
			alloc_size = struct_size(mbox_out, ents, remain_feats);
			copy_feats = remain_feats;
			remain_feats = 0;
		}

		memset(&mbox_in, 0, sizeof(mbox_in));
		mbox_in.count = cpu_to_le32(alloc_size);
		mbox_in.start_idx = cpu_to_le16(start);
		memset(mbox_out, 0, alloc_size);
		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_GET_SUPPORTED_FEATURES,
			.size_in = sizeof(mbox_in),
			.payload_in = &mbox_in,
			.size_out = alloc_size,
			.payload_out = mbox_out,
			.min_out = hdr_size,
		};
		rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
		if (rc < 0)
			return NULL;

		if (mbox_cmd.size_out <= hdr_size)
			return NULL;

		/*
		 * Make sure retrieved out buffer is multiple of feature
		 * entries.
		 */
		retrieved = mbox_cmd.size_out - hdr_size;
		if (retrieved % feat_size)
			return NULL;

		num_entries = le16_to_cpu(mbox_out->num_entries);
		/*
		 * If the reported output entries * defined entry size !=
		 * retrieved output bytes, then the output package is incorrect.
		 */
		if (num_entries * feat_size != retrieved)
			return NULL;

		memcpy(entry, mbox_out->ents, retrieved);
		entry += num_entries;
		/*
		 * If the number of output entries is less than expected, add the
		 * remaining entries to the next batch.
		 */
		remain_feats += copy_feats - num_entries;
		start += num_entries;
	} while (remain_feats);

	entries->num_features = count;

	return no_free_ptr(entries);
}

static void free_cxlfs(void *_cxlfs)
{
	struct cxl_features_state *cxlfs = _cxlfs;
	struct cxl_dev_state *cxlds = cxlfs->cxlds;

	cxlds->cxlfs = NULL;
	kvfree(cxlfs->entries);
	kfree(cxlfs);
}

/**
 * devm_cxl_setup_features() - Allocate and initialize features context
 * @cxlds: CXL device context
 *
 * Return 0 on success or -errno on failure.
 */
int devm_cxl_setup_features(struct cxl_dev_state *cxlds)
{
	struct cxl_mailbox *cxl_mbox = &cxlds->cxl_mbox;

	if (cxl_mbox->feat_cap < CXL_FEATURES_RO)
		return -ENODEV;

	struct cxl_features_state *cxlfs __free(kfree) =
		kzalloc(sizeof(*cxlfs), GFP_KERNEL);
	if (!cxlfs)
		return -ENOMEM;

	cxlfs->cxlds = cxlds;

	cxlfs->entries = get_supported_features(cxlfs);
	if (!cxlfs->entries)
		return -ENOMEM;

	cxlds->cxlfs = cxlfs;

	return devm_add_action_or_reset(cxlds->dev, free_cxlfs, no_free_ptr(cxlfs));
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_setup_features, "CXL");

size_t cxl_get_feature(struct cxl_mailbox *cxl_mbox, const uuid_t *feat_uuid,
		       enum cxl_get_feat_selection selection,
		       void *feat_out, size_t feat_out_size, u16 offset,
		       u16 *return_code)
{
	size_t data_to_rd_size, size_out;
	struct cxl_mbox_get_feat_in pi;
	struct cxl_mbox_cmd mbox_cmd;
	size_t data_rcvd_size = 0;
	int rc;

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_INPUT;

	if (!feat_out || !feat_out_size)
		return 0;

	size_out = min(feat_out_size, cxl_mbox->payload_size);
	uuid_copy(&pi.uuid, feat_uuid);
	pi.selection = selection;
	do {
		data_to_rd_size = min(feat_out_size - data_rcvd_size,
				      cxl_mbox->payload_size);
		pi.offset = cpu_to_le16(offset + data_rcvd_size);
		pi.count = cpu_to_le16(data_to_rd_size);

		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_GET_FEATURE,
			.size_in = sizeof(pi),
			.payload_in = &pi,
			.size_out = size_out,
			.payload_out = feat_out + data_rcvd_size,
			.min_out = data_to_rd_size,
		};
		rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
		if (rc < 0 || !mbox_cmd.size_out) {
			if (return_code)
				*return_code = mbox_cmd.return_code;
			return 0;
		}
		data_rcvd_size += mbox_cmd.size_out;
	} while (data_rcvd_size < feat_out_size);

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_SUCCESS;

	return data_rcvd_size;
}
