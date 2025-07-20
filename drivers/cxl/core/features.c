// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/fwctl.h>
#include <linux/device.h>
#include <cxl/mailbox.h>
#include <cxl/features.h>
#include <uapi/fwctl/cxl.h>
#include "cxl.h"
#include "core.h"
#include "cxlmem.h"

/**
 * DOC: cxl features
 *
 * CXL Features:
 * A CXL device that includes a mailbox supports commands that allows
 * listing, getting, and setting of optionally defined features such
 * as memory sparing or post package sparing. Vendors may define custom
 * features for the device.
 */

/* All the features below are exclusive to the kernel */
static const uuid_t cxl_exclusive_feats[] = {
	CXL_FEAT_PATROL_SCRUB_UUID,
	CXL_FEAT_ECS_UUID,
	CXL_FEAT_SPPR_UUID,
	CXL_FEAT_HPPR_UUID,
	CXL_FEAT_CACHELINE_SPARING_UUID,
	CXL_FEAT_ROW_SPARING_UUID,
	CXL_FEAT_BANK_SPARING_UUID,
	CXL_FEAT_RANK_SPARING_UUID,
};

static bool is_cxl_feature_exclusive_by_uuid(const uuid_t *uuid)
{
	for (int i = 0; i < ARRAY_SIZE(cxl_exclusive_feats); i++) {
		if (uuid_equal(uuid, &cxl_exclusive_feats[i]))
			return true;
	}

	return false;
}

static bool is_cxl_feature_exclusive(struct cxl_feat_entry *entry)
{
	return is_cxl_feature_exclusive_by_uuid(&entry->uuid);
}

struct cxl_features_state *to_cxlfs(struct cxl_dev_state *cxlds)
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
	int user_feats = 0;
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
		for (int i = 0; i < num_entries; i++) {
			if (!is_cxl_feature_exclusive(entry + i))
				user_feats++;
		}
		entry += num_entries;
		/*
		 * If the number of output entries is less than expected, add the
		 * remaining entries to the next batch.
		 */
		remain_feats += copy_feats - num_entries;
		start += num_entries;
	} while (remain_feats);

	entries->num_features = count;
	entries->num_user_features = user_feats;

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

/*
 * FEAT_DATA_MIN_PAYLOAD_SIZE - min extra number of bytes should be
 * available in the mailbox for storing the actual feature data so that
 * the feature data transfer would work as expected.
 */
#define FEAT_DATA_MIN_PAYLOAD_SIZE 10
int cxl_set_feature(struct cxl_mailbox *cxl_mbox,
		    const uuid_t *feat_uuid, u8 feat_version,
		    const void *feat_data, size_t feat_data_size,
		    u32 feat_flag, u16 offset, u16 *return_code)
{
	size_t data_in_size, data_sent_size = 0;
	struct cxl_mbox_cmd mbox_cmd;
	size_t hdr_size;

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_INPUT;

	struct cxl_mbox_set_feat_in *pi __free(kfree) =
			kzalloc(cxl_mbox->payload_size, GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	uuid_copy(&pi->uuid, feat_uuid);
	pi->version = feat_version;
	feat_flag &= ~CXL_SET_FEAT_FLAG_DATA_TRANSFER_MASK;
	feat_flag |= CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET;
	hdr_size = sizeof(pi->hdr);
	/*
	 * Check minimum mbox payload size is available for
	 * the feature data transfer.
	 */
	if (hdr_size + FEAT_DATA_MIN_PAYLOAD_SIZE > cxl_mbox->payload_size)
		return -ENOMEM;

	if (hdr_size + feat_data_size <= cxl_mbox->payload_size) {
		pi->flags = cpu_to_le32(feat_flag |
					CXL_SET_FEAT_FLAG_FULL_DATA_TRANSFER);
		data_in_size = feat_data_size;
	} else {
		pi->flags = cpu_to_le32(feat_flag |
					CXL_SET_FEAT_FLAG_INITIATE_DATA_TRANSFER);
		data_in_size = cxl_mbox->payload_size - hdr_size;
	}

	do {
		int rc;

		pi->offset = cpu_to_le16(offset + data_sent_size);
		memcpy(pi->feat_data, feat_data + data_sent_size, data_in_size);
		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_SET_FEATURE,
			.size_in = hdr_size + data_in_size,
			.payload_in = pi,
		};
		rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
		if (rc < 0) {
			if (return_code)
				*return_code = mbox_cmd.return_code;
			return rc;
		}

		data_sent_size += data_in_size;
		if (data_sent_size >= feat_data_size) {
			if (return_code)
				*return_code = CXL_MBOX_CMD_RC_SUCCESS;
			return 0;
		}

		if ((feat_data_size - data_sent_size) <= (cxl_mbox->payload_size - hdr_size)) {
			data_in_size = feat_data_size - data_sent_size;
			pi->flags = cpu_to_le32(feat_flag |
						CXL_SET_FEAT_FLAG_FINISH_DATA_TRANSFER);
		} else {
			pi->flags = cpu_to_le32(feat_flag |
						CXL_SET_FEAT_FLAG_CONTINUE_DATA_TRANSFER);
		}
	} while (true);
}

/* FWCTL support */

static inline struct cxl_memdev *fwctl_to_memdev(struct fwctl_device *fwctl_dev)
{
	return to_cxl_memdev(fwctl_dev->dev.parent);
}

static int cxlctl_open_uctx(struct fwctl_uctx *uctx)
{
	return 0;
}

static void cxlctl_close_uctx(struct fwctl_uctx *uctx)
{
}

struct cxl_feat_entry *
cxl_feature_info(struct cxl_features_state *cxlfs,
		 const uuid_t *uuid)
{
	struct cxl_feat_entry *feat;

	for (int i = 0; i < cxlfs->entries->num_features; i++) {
		feat = &cxlfs->entries->ent[i];
		if (uuid_equal(uuid, &feat->uuid))
			return feat;
	}

	return ERR_PTR(-EINVAL);
}

static void *cxlctl_get_supported_features(struct cxl_features_state *cxlfs,
					   const struct fwctl_rpc_cxl *rpc_in,
					   size_t *out_len)
{
	const struct cxl_mbox_get_sup_feats_in *feat_in;
	struct cxl_mbox_get_sup_feats_out *feat_out;
	struct cxl_feat_entry *pos;
	size_t out_size;
	int requested;
	u32 count;
	u16 start;
	int i;

	if (rpc_in->op_size != sizeof(*feat_in))
		return ERR_PTR(-EINVAL);

	feat_in = &rpc_in->get_sup_feats_in;
	count = le32_to_cpu(feat_in->count);
	start = le16_to_cpu(feat_in->start_idx);
	requested = count / sizeof(*pos);

	/*
	 * Make sure that the total requested number of entries is not greater
	 * than the total number of supported features allowed for userspace.
	 */
	if (start >= cxlfs->entries->num_features)
		return ERR_PTR(-EINVAL);

	requested = min_t(int, requested, cxlfs->entries->num_features - start);

	out_size = sizeof(struct fwctl_rpc_cxl_out) +
		struct_size(feat_out, ents, requested);

	struct fwctl_rpc_cxl_out *rpc_out __free(kvfree) =
		kvzalloc(out_size, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	rpc_out->size = struct_size(feat_out, ents, requested);
	feat_out = &rpc_out->get_sup_feats_out;

	for (i = start, pos = &feat_out->ents[0];
	     i < cxlfs->entries->num_features; i++, pos++) {
		if (i - start == requested)
			break;

		memcpy(pos, &cxlfs->entries->ent[i], sizeof(*pos));
		/*
		 * If the feature is exclusive, set the set_feat_size to 0 to
		 * indicate that the feature is not changeable.
		 */
		if (is_cxl_feature_exclusive(pos)) {
			u32 flags;

			pos->set_feat_size = 0;
			flags = le32_to_cpu(pos->flags);
			flags &= ~CXL_FEATURE_F_CHANGEABLE;
			pos->flags = cpu_to_le32(flags);
		}
	}

	feat_out->num_entries = cpu_to_le16(requested);
	feat_out->supported_feats = cpu_to_le16(cxlfs->entries->num_features);
	rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;
	*out_len = out_size;

	return no_free_ptr(rpc_out);
}

static void *cxlctl_get_feature(struct cxl_features_state *cxlfs,
				const struct fwctl_rpc_cxl *rpc_in,
				size_t *out_len)
{
	struct cxl_mailbox *cxl_mbox = &cxlfs->cxlds->cxl_mbox;
	const struct cxl_mbox_get_feat_in *feat_in;
	u16 offset, count, return_code;
	size_t out_size = *out_len;

	if (rpc_in->op_size != sizeof(*feat_in))
		return ERR_PTR(-EINVAL);

	feat_in = &rpc_in->get_feat_in;
	offset = le16_to_cpu(feat_in->offset);
	count = le16_to_cpu(feat_in->count);

	if (!count)
		return ERR_PTR(-EINVAL);

	struct fwctl_rpc_cxl_out *rpc_out __free(kvfree) =
		kvzalloc(out_size, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	out_size = cxl_get_feature(cxl_mbox, &feat_in->uuid,
				   feat_in->selection, rpc_out->payload,
				   count, offset, &return_code);
	*out_len = sizeof(struct fwctl_rpc_cxl_out);
	if (!out_size) {
		rpc_out->size = 0;
		rpc_out->retval = return_code;
		return no_free_ptr(rpc_out);
	}

	rpc_out->size = out_size;
	rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;
	*out_len += out_size;

	return no_free_ptr(rpc_out);
}

static void *cxlctl_set_feature(struct cxl_features_state *cxlfs,
				const struct fwctl_rpc_cxl *rpc_in,
				size_t *out_len)
{
	struct cxl_mailbox *cxl_mbox = &cxlfs->cxlds->cxl_mbox;
	const struct cxl_mbox_set_feat_in *feat_in;
	size_t out_size, data_size;
	u16 offset, return_code;
	u32 flags;
	int rc;

	if (rpc_in->op_size <= sizeof(feat_in->hdr))
		return ERR_PTR(-EINVAL);

	feat_in = &rpc_in->set_feat_in;

	if (is_cxl_feature_exclusive_by_uuid(&feat_in->uuid))
		return ERR_PTR(-EPERM);

	offset = le16_to_cpu(feat_in->offset);
	flags = le32_to_cpu(feat_in->flags);
	out_size = *out_len;

	struct fwctl_rpc_cxl_out *rpc_out __free(kvfree) =
		kvzalloc(out_size, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	rpc_out->size = 0;

	data_size = rpc_in->op_size - sizeof(feat_in->hdr);
	rc = cxl_set_feature(cxl_mbox, &feat_in->uuid,
			     feat_in->version, feat_in->feat_data,
			     data_size, flags, offset, &return_code);
	*out_len = sizeof(*rpc_out);
	if (rc) {
		rpc_out->retval = return_code;
		return no_free_ptr(rpc_out);
	}

	rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;

	return no_free_ptr(rpc_out);
}

static bool cxlctl_validate_set_features(struct cxl_features_state *cxlfs,
					 const struct fwctl_rpc_cxl *rpc_in,
					 enum fwctl_rpc_scope scope)
{
	u16 effects, imm_mask, reset_mask;
	struct cxl_feat_entry *feat;
	u32 flags;

	if (rpc_in->op_size < sizeof(uuid_t))
		return false;

	feat = cxl_feature_info(cxlfs, &rpc_in->set_feat_in.uuid);
	if (IS_ERR(feat))
		return false;

	/* Ensure that the attribute is changeable */
	flags = le32_to_cpu(feat->flags);
	if (!(flags & CXL_FEATURE_F_CHANGEABLE))
		return false;

	effects = le16_to_cpu(feat->effects);

	/*
	 * Reserved bits are set, rejecting since the effects is not
	 * comprehended by the driver.
	 */
	if (effects & CXL_CMD_EFFECTS_RESERVED) {
		dev_warn_once(cxlfs->cxlds->dev,
			      "Reserved bits set in the Feature effects field!\n");
		return false;
	}

	/* Currently no user background command support */
	if (effects & CXL_CMD_BACKGROUND)
		return false;

	/* Effects cause immediate change, highest security scope is needed */
	imm_mask = CXL_CMD_CONFIG_CHANGE_IMMEDIATE |
		   CXL_CMD_DATA_CHANGE_IMMEDIATE |
		   CXL_CMD_POLICY_CHANGE_IMMEDIATE |
		   CXL_CMD_LOG_CHANGE_IMMEDIATE;

	reset_mask = CXL_CMD_CONFIG_CHANGE_COLD_RESET |
		     CXL_CMD_CONFIG_CHANGE_CONV_RESET |
		     CXL_CMD_CONFIG_CHANGE_CXL_RESET;

	/* If no immediate or reset effect set, The hardware has a bug */
	if (!(effects & imm_mask) && !(effects & reset_mask))
		return false;

	/*
	 * If the Feature setting causes immediate configuration change
	 * then we need the full write permission policy.
	 */
	if (effects & imm_mask && scope >= FWCTL_RPC_DEBUG_WRITE_FULL)
		return true;

	/*
	 * If the Feature setting only causes configuration change
	 * after a reset, then the lesser level of write permission
	 * policy is ok.
	 */
	if (!(effects & imm_mask) && scope >= FWCTL_RPC_DEBUG_WRITE)
		return true;

	return false;
}

static bool cxlctl_validate_hw_command(struct cxl_features_state *cxlfs,
				       const struct fwctl_rpc_cxl *rpc_in,
				       enum fwctl_rpc_scope scope,
				       u16 opcode)
{
	struct cxl_mailbox *cxl_mbox = &cxlfs->cxlds->cxl_mbox;

	switch (opcode) {
	case CXL_MBOX_OP_GET_SUPPORTED_FEATURES:
	case CXL_MBOX_OP_GET_FEATURE:
		return cxl_mbox->feat_cap >= CXL_FEATURES_RO;
	case CXL_MBOX_OP_SET_FEATURE:
		if (cxl_mbox->feat_cap < CXL_FEATURES_RW)
			return false;
		return cxlctl_validate_set_features(cxlfs, rpc_in, scope);
	default:
		return false;
	}
}

static void *cxlctl_handle_commands(struct cxl_features_state *cxlfs,
				    const struct fwctl_rpc_cxl *rpc_in,
				    size_t *out_len, u16 opcode)
{
	switch (opcode) {
	case CXL_MBOX_OP_GET_SUPPORTED_FEATURES:
		return cxlctl_get_supported_features(cxlfs, rpc_in, out_len);
	case CXL_MBOX_OP_GET_FEATURE:
		return cxlctl_get_feature(cxlfs, rpc_in, out_len);
	case CXL_MBOX_OP_SET_FEATURE:
		return cxlctl_set_feature(cxlfs, rpc_in, out_len);
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static void *cxlctl_fw_rpc(struct fwctl_uctx *uctx, enum fwctl_rpc_scope scope,
			   void *in, size_t in_len, size_t *out_len)
{
	struct fwctl_device *fwctl_dev = uctx->fwctl;
	struct cxl_memdev *cxlmd = fwctl_to_memdev(fwctl_dev);
	struct cxl_features_state *cxlfs = to_cxlfs(cxlmd->cxlds);
	const struct fwctl_rpc_cxl *rpc_in = in;
	u16 opcode = rpc_in->opcode;

	if (!cxlctl_validate_hw_command(cxlfs, rpc_in, scope, opcode))
		return ERR_PTR(-EINVAL);

	return cxlctl_handle_commands(cxlfs, rpc_in, out_len, opcode);
}

static const struct fwctl_ops cxlctl_ops = {
	.device_type = FWCTL_DEVICE_TYPE_CXL,
	.uctx_size = sizeof(struct fwctl_uctx),
	.open_uctx = cxlctl_open_uctx,
	.close_uctx = cxlctl_close_uctx,
	.fw_rpc = cxlctl_fw_rpc,
};

DEFINE_FREE(free_fwctl_dev, struct fwctl_device *, if (_T) fwctl_put(_T))

static void free_memdev_fwctl(void *_fwctl_dev)
{
	struct fwctl_device *fwctl_dev = _fwctl_dev;

	fwctl_unregister(fwctl_dev);
	fwctl_put(fwctl_dev);
}

int devm_cxl_setup_fwctl(struct device *host, struct cxl_memdev *cxlmd)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_features_state *cxlfs;
	int rc;

	cxlfs = to_cxlfs(cxlds);
	if (!cxlfs)
		return -ENODEV;

	/* No need to setup FWCTL if there are no user allowed features found */
	if (!cxlfs->entries->num_user_features)
		return -ENODEV;

	struct fwctl_device *fwctl_dev __free(free_fwctl_dev) =
		_fwctl_alloc_device(&cxlmd->dev, &cxlctl_ops, sizeof(*fwctl_dev));
	if (!fwctl_dev)
		return -ENOMEM;

	rc = fwctl_register(fwctl_dev);
	if (rc)
		return rc;

	return devm_add_action_or_reset(host, free_memdev_fwctl,
					no_free_ptr(fwctl_dev));
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_setup_fwctl, "CXL");

MODULE_IMPORT_NS("FWCTL");
