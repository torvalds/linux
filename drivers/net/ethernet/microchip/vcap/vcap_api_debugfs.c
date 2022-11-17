// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API debug file system support
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 */

#include "vcap_api_private.h"
#include "vcap_api_debugfs.h"

struct vcap_admin_debugfs_info {
	struct vcap_control *vctrl;
	struct vcap_admin *admin;
};

struct vcap_port_debugfs_info {
	struct vcap_control *vctrl;
	struct net_device *ndev;
};

static bool vcap_bitarray_zero(int width, u8 *value)
{
	int bytes = DIV_ROUND_UP(width, BITS_PER_BYTE);
	u8 total = 0, bmask = 0xff;
	int rwidth = width;
	int idx;

	for (idx = 0; idx < bytes; ++idx, rwidth -= BITS_PER_BYTE) {
		if (rwidth && rwidth < BITS_PER_BYTE)
			bmask = (1 << rwidth) - 1;
		total += value[idx] & bmask;
	}
	return total == 0;
}

static bool vcap_get_bit(u32 *stream, struct vcap_stream_iter *itr)
{
	u32 mask = BIT(itr->reg_bitpos);
	u32 *p = &stream[itr->reg_idx];

	return !!(*p & mask);
}

static void vcap_decode_field(u32 *stream, struct vcap_stream_iter *itr,
			      int width, u8 *value)
{
	int idx;

	/* Loop over the field value bits and get the field bits and
	 * set them in the output value byte array
	 */
	for (idx = 0; idx < width; idx++) {
		u8 bidx = idx & 0x7;

		/* Decode one field value bit */
		if (vcap_get_bit(stream, itr))
			*value |= 1 << bidx;
		vcap_iter_next(itr);
		if (bidx == 7)
			value++;
	}
}

/* Verify that the typegroup bits have the correct values */
static int vcap_verify_typegroups(u32 *stream, int sw_width,
				  const struct vcap_typegroup *tgt, bool mask,
				  int sw_max)
{
	struct vcap_stream_iter iter;
	int sw_cnt, idx;

	vcap_iter_set(&iter, sw_width, tgt, 0);
	sw_cnt = 0;
	while (iter.tg->width) {
		u32 value = 0;
		u32 tg_value = iter.tg->value;

		if (mask)
			tg_value = (1 << iter.tg->width) - 1;
		/* Set position to current typegroup bit */
		iter.offset = iter.tg->offset;
		vcap_iter_update(&iter);
		for (idx = 0; idx < iter.tg->width; idx++) {
			/* Decode one typegroup bit */
			if (vcap_get_bit(stream, &iter))
				value |= 1 << idx;
			iter.offset++;
			vcap_iter_update(&iter);
		}
		if (value != tg_value)
			return -EINVAL;
		iter.tg++; /* next typegroup */
		sw_cnt++;
		/* Stop checking more typegroups */
		if (sw_max && sw_cnt >= sw_max)
			break;
	}
	return 0;
}

/* Find the subword width of the key typegroup that matches the stream data */
static int vcap_find_keystream_typegroup_sw(struct vcap_control *vctrl,
					    enum vcap_type vt, u32 *stream,
					    bool mask, int sw_max)
{
	const struct vcap_typegroup **tgt;
	int sw_idx, res;

	tgt = vctrl->vcaps[vt].keyfield_set_typegroups;
	/* Try the longest subword match first */
	for (sw_idx = vctrl->vcaps[vt].sw_count; sw_idx >= 0; sw_idx--) {
		if (!tgt[sw_idx])
			continue;

		res = vcap_verify_typegroups(stream, vctrl->vcaps[vt].sw_width,
					     tgt[sw_idx], mask, sw_max);
		if (res == 0)
			return sw_idx;
	}
	return -EINVAL;
}

/* Find the subword width of the action typegroup that matches the stream data
 */
static int vcap_find_actionstream_typegroup_sw(struct vcap_control *vctrl,
					       enum vcap_type vt, u32 *stream,
					       int sw_max)
{
	const struct vcap_typegroup **tgt;
	int sw_idx, res;

	tgt = vctrl->vcaps[vt].actionfield_set_typegroups;
	/* Try the longest subword match first */
	for (sw_idx = vctrl->vcaps[vt].sw_count; sw_idx >= 0; sw_idx--) {
		if (!tgt[sw_idx])
			continue;
		res = vcap_verify_typegroups(stream, vctrl->vcaps[vt].act_width,
					     tgt[sw_idx], false, sw_max);
		if (res == 0)
			return sw_idx;
	}
	return -EINVAL;
}

/* Verify that the type id in the stream matches the type id of the keyset */
static bool vcap_verify_keystream_keyset(struct vcap_control *vctrl,
					 enum vcap_type vt,
					 u32 *keystream,
					 u32 *mskstream,
					 enum vcap_keyfield_set keyset)
{
	const struct vcap_info *vcap = &vctrl->vcaps[vt];
	const struct vcap_field *typefld;
	const struct vcap_typegroup *tgt;
	const struct vcap_field *fields;
	struct vcap_stream_iter iter;
	const struct vcap_set *info;
	u32 value = 0;
	u32 mask = 0;

	if (vcap_keyfield_count(vctrl, vt, keyset) == 0)
		return false;

	info = vcap_keyfieldset(vctrl, vt, keyset);
	/* Check that the keyset is valid */
	if (!info)
		return false;

	/* a type_id of value -1 means that there is no type field */
	if (info->type_id == (u8)-1)
		return true;

	/* Get a valid typegroup for the specific keyset */
	tgt = vcap_keyfield_typegroup(vctrl, vt, keyset);
	if (!tgt)
		return false;

	fields = vcap_keyfields(vctrl, vt, keyset);
	if (!fields)
		return false;

	typefld = &fields[VCAP_KF_TYPE];
	vcap_iter_init(&iter, vcap->sw_width, tgt, typefld->offset);
	vcap_decode_field(mskstream, &iter, typefld->width, (u8 *)&mask);
	/* no type info if there are no mask bits */
	if (vcap_bitarray_zero(typefld->width, (u8 *)&mask))
		return false;

	/* Get the value of the type field in the stream and compare to the
	 * one define in the vcap keyset
	 */
	vcap_iter_init(&iter, vcap->sw_width, tgt, typefld->offset);
	vcap_decode_field(keystream, &iter, typefld->width, (u8 *)&value);

	return (value == info->type_id);
}

/* Verify that the typegroup information, subword count, keyset and type id
 * are in sync and correct, return the keyset
 */
static enum
vcap_keyfield_set vcap_find_keystream_keyset(struct vcap_control *vctrl,
					     enum vcap_type vt,
					     u32 *keystream,
					     u32 *mskstream,
					     bool mask, int sw_max)
{
	const struct vcap_set *keyfield_set;
	int sw_count, idx;
	bool res;

	sw_count = vcap_find_keystream_typegroup_sw(vctrl, vt, keystream, mask,
						    sw_max);
	if (sw_count < 0)
		return sw_count;

	keyfield_set = vctrl->vcaps[vt].keyfield_set;
	for (idx = 0; idx < vctrl->vcaps[vt].keyfield_set_size; ++idx) {
		if (keyfield_set[idx].sw_per_item != sw_count)
			continue;

		res = vcap_verify_keystream_keyset(vctrl, vt, keystream,
						   mskstream, idx);
		if (res)
			return idx;
	}
	return -EINVAL;
}

/* Read key data from a VCAP address and discover if there is a rule keyset
 * here
 */
static bool
vcap_verify_actionstream_actionset(struct vcap_control *vctrl,
				   enum vcap_type vt,
				   u32 *actionstream,
				   enum vcap_actionfield_set actionset)
{
	const struct vcap_typegroup *tgt;
	const struct vcap_field *fields;
	const struct vcap_set *info;

	if (vcap_actionfield_count(vctrl, vt, actionset) == 0)
		return false;

	info = vcap_actionfieldset(vctrl, vt, actionset);
	/* Check that the actionset is valid */
	if (!info)
		return false;

	/* a type_id of value -1 means that there is no type field */
	if (info->type_id == (u8)-1)
		return true;

	/* Get a valid typegroup for the specific actionset */
	tgt = vcap_actionfield_typegroup(vctrl, vt, actionset);
	if (!tgt)
		return false;

	fields = vcap_actionfields(vctrl, vt, actionset);
	if (!fields)
		return false;

	/* Later this will be expanded with a check of the type id */
	return true;
}

/* Verify that the typegroup information, subword count, actionset and type id
 * are in sync and correct, return the actionset
 */
static enum vcap_actionfield_set
vcap_find_actionstream_actionset(struct vcap_control *vctrl,
				 enum vcap_type vt,
				 u32 *stream,
				 int sw_max)
{
	const struct vcap_set *actionfield_set;
	int sw_count, idx;
	bool res;

	sw_count = vcap_find_actionstream_typegroup_sw(vctrl, vt, stream,
						       sw_max);
	if (sw_count < 0)
		return sw_count;

	actionfield_set = vctrl->vcaps[vt].actionfield_set;
	for (idx = 0; idx < vctrl->vcaps[vt].actionfield_set_size; ++idx) {
		if (actionfield_set[idx].sw_per_item != sw_count)
			continue;

		res = vcap_verify_actionstream_actionset(vctrl, vt,
							 stream, idx);
		if (res)
			return idx;
	}
	return -EINVAL;
}

/* Read key data from a VCAP address and discover if there is a rule keyset
 * here
 */
static int vcap_addr_keyset(struct vcap_control *vctrl,
			    struct net_device *ndev,
			    struct vcap_admin *admin,
			    int addr)
{
	enum vcap_type vt = admin->vtype;
	int keyset_sw_regs, idx;
	u32 key = 0, mask = 0;

	/* Read the cache at the specified address */
	keyset_sw_regs = DIV_ROUND_UP(vctrl->vcaps[vt].sw_width, 32);
	vctrl->ops->update(ndev, admin, VCAP_CMD_READ, VCAP_SEL_ALL, addr);
	vctrl->ops->cache_read(ndev, admin, VCAP_SEL_ENTRY, 0,
			       keyset_sw_regs);
	/* Skip uninitialized key/mask entries */
	for (idx = 0; idx < keyset_sw_regs; ++idx) {
		key |= ~admin->cache.keystream[idx];
		mask |= admin->cache.maskstream[idx];
	}
	if (key == 0 && mask == 0)
		return -EINVAL;
	/* Decode and locate the keyset */
	return vcap_find_keystream_keyset(vctrl, vt, admin->cache.keystream,
					  admin->cache.maskstream, false, 0);
}

static int vcap_read_rule(struct vcap_rule_internal *ri)
{
	struct vcap_admin *admin = ri->admin;
	int sw_idx, ent_idx = 0, act_idx = 0;
	u32 addr = ri->addr;

	if (!ri->size || !ri->keyset_sw_regs || !ri->actionset_sw_regs) {
		pr_err("%s:%d: rule is empty\n", __func__, __LINE__);
		return -EINVAL;
	}
	vcap_erase_cache(ri);
	/* Use the values in the streams to read the VCAP cache */
	for (sw_idx = 0; sw_idx < ri->size; sw_idx++, addr++) {
		ri->vctrl->ops->update(ri->ndev, admin, VCAP_CMD_READ,
				       VCAP_SEL_ALL, addr);
		ri->vctrl->ops->cache_read(ri->ndev, admin,
					   VCAP_SEL_ENTRY, ent_idx,
					   ri->keyset_sw_regs);
		ri->vctrl->ops->cache_read(ri->ndev, admin,
					   VCAP_SEL_ACTION, act_idx,
					   ri->actionset_sw_regs);
		if (sw_idx == 0)
			ri->vctrl->ops->cache_read(ri->ndev, admin,
						   VCAP_SEL_COUNTER,
						   ri->counter_id, 0);
		ent_idx += ri->keyset_sw_regs;
		act_idx += ri->actionset_sw_regs;
	}
	return 0;
}

/* Dump the keyfields value and mask values */
static void vcap_debugfs_show_rule_keyfield(struct vcap_control *vctrl,
					    struct vcap_output_print *out,
					    enum vcap_key_field key,
					    const struct vcap_field *keyfield,
					    u8 *value, u8 *mask)
{
	bool hex = false;
	int idx, bytes;

	out->prf(out->dst, "    %s: W%d: ", vcap_keyfield_name(vctrl, key),
		 keyfield[key].width);

	switch (keyfield[key].type) {
	case VCAP_FIELD_BIT:
		out->prf(out->dst, "%d/%d", value[0], mask[0]);
		break;
	case VCAP_FIELD_U32:
		if (key == VCAP_KF_L3_IP4_SIP || key == VCAP_KF_L3_IP4_DIP) {
			out->prf(out->dst, "%pI4h/%pI4h", value, mask);
		} else if (key == VCAP_KF_ETYPE ||
			   key == VCAP_KF_IF_IGR_PORT_MASK) {
			hex = true;
		} else {
			u32 fmsk = (1 << keyfield[key].width) - 1;
			u32 val = *(u32 *)value;
			u32 msk = *(u32 *)mask;

			out->prf(out->dst, "%u/%u", val & fmsk, msk & fmsk);
		}
		break;
	case VCAP_FIELD_U48:
		if (key == VCAP_KF_L2_SMAC || key == VCAP_KF_L2_DMAC)
			out->prf(out->dst, "%pMR/%pMR", value, mask);
		else
			hex = true;
		break;
	case VCAP_FIELD_U56:
	case VCAP_FIELD_U64:
	case VCAP_FIELD_U72:
	case VCAP_FIELD_U112:
		hex = true;
		break;
	case VCAP_FIELD_U128:
		if (key == VCAP_KF_L3_IP6_SIP || key == VCAP_KF_L3_IP6_DIP) {
			u8 nvalue[16], nmask[16];

			vcap_netbytes_copy(nvalue, value, sizeof(nvalue));
			vcap_netbytes_copy(nmask, mask, sizeof(nmask));
			out->prf(out->dst, "%pI6/%pI6", nvalue, nmask);
		} else {
			hex = true;
		}
		break;
	}
	if (hex) {
		bytes = DIV_ROUND_UP(keyfield[key].width, BITS_PER_BYTE);
		out->prf(out->dst, "0x");
		for (idx = 0; idx < bytes; ++idx)
			out->prf(out->dst, "%02x", value[bytes - idx - 1]);
		out->prf(out->dst, "/0x");
		for (idx = 0; idx < bytes; ++idx)
			out->prf(out->dst, "%02x", mask[bytes - idx - 1]);
	}
	out->prf(out->dst, "\n");
}

static void
vcap_debugfs_show_rule_actionfield(struct vcap_control *vctrl,
				   struct vcap_output_print *out,
				   enum vcap_action_field action,
				   const struct vcap_field *actionfield,
				   u8 *value)
{
	bool hex = false;
	int idx, bytes;
	u32 fmsk, val;

	out->prf(out->dst, "    %s: W%d: ",
		 vcap_actionfield_name(vctrl, action),
		 actionfield[action].width);

	switch (actionfield[action].type) {
	case VCAP_FIELD_BIT:
		out->prf(out->dst, "%d", value[0]);
		break;
	case VCAP_FIELD_U32:
		fmsk = (1 << actionfield[action].width) - 1;
		val = *(u32 *)value;
		out->prf(out->dst, "%u", val & fmsk);
		break;
	case VCAP_FIELD_U48:
	case VCAP_FIELD_U56:
	case VCAP_FIELD_U64:
	case VCAP_FIELD_U72:
	case VCAP_FIELD_U112:
	case VCAP_FIELD_U128:
		hex = true;
		break;
	}
	if (hex) {
		bytes = DIV_ROUND_UP(actionfield[action].width, BITS_PER_BYTE);
		out->prf(out->dst, "0x");
		for (idx = 0; idx < bytes; ++idx)
			out->prf(out->dst, "%02x", value[bytes - idx - 1]);
	}
	out->prf(out->dst, "\n");
}

static int vcap_debugfs_show_rule_keyset(struct vcap_rule_internal *ri,
					 struct vcap_output_print *out)
{
	struct vcap_control *vctrl = ri->vctrl;
	struct vcap_stream_iter kiter, miter;
	struct vcap_admin *admin = ri->admin;
	const struct vcap_field *keyfield;
	enum vcap_type vt = admin->vtype;
	const struct vcap_typegroup *tgt;
	enum vcap_keyfield_set keyset;
	int idx, res, keyfield_count;
	u32 *maskstream;
	u32 *keystream;
	u8 value[16];
	u8 mask[16];

	keystream = admin->cache.keystream;
	maskstream = admin->cache.maskstream;
	res = vcap_find_keystream_keyset(vctrl, vt, keystream, maskstream,
					 false, 0);
	if (res < 0) {
		pr_err("%s:%d: could not find valid keyset: %d\n",
		       __func__, __LINE__, res);
		return -EINVAL;
	}
	keyset = res;
	out->prf(out->dst, "  keyset: %s\n",
		 vcap_keyset_name(vctrl, ri->data.keyset));
	out->prf(out->dst, "  keyset_sw: %d\n", ri->keyset_sw);
	out->prf(out->dst, "  keyset_sw_regs: %d\n", ri->keyset_sw_regs);
	keyfield_count = vcap_keyfield_count(vctrl, vt, keyset);
	keyfield = vcap_keyfields(vctrl, vt, keyset);
	tgt = vcap_keyfield_typegroup(vctrl, vt, keyset);
	/* Start decoding the streams */
	for (idx = 0; idx < keyfield_count; ++idx) {
		if (keyfield[idx].width <= 0)
			continue;
		/* First get the mask */
		memset(mask, 0, DIV_ROUND_UP(keyfield[idx].width, 8));
		vcap_iter_init(&miter, vctrl->vcaps[vt].sw_width, tgt,
			       keyfield[idx].offset);
		vcap_decode_field(maskstream, &miter, keyfield[idx].width,
				  mask);
		/* Skip if no mask bits are set */
		if (vcap_bitarray_zero(keyfield[idx].width, mask))
			continue;
		/* Get the key */
		memset(value, 0, DIV_ROUND_UP(keyfield[idx].width, 8));
		vcap_iter_init(&kiter, vctrl->vcaps[vt].sw_width, tgt,
			       keyfield[idx].offset);
		vcap_decode_field(keystream, &kiter, keyfield[idx].width,
				  value);
		vcap_debugfs_show_rule_keyfield(vctrl, out, idx, keyfield,
						value, mask);
	}
	return 0;
}

static int vcap_debugfs_show_rule_actionset(struct vcap_rule_internal *ri,
					    struct vcap_output_print *out)
{
	struct vcap_control *vctrl = ri->vctrl;
	struct vcap_admin *admin = ri->admin;
	const struct vcap_field *actionfield;
	enum vcap_actionfield_set actionset;
	enum vcap_type vt = admin->vtype;
	const struct vcap_typegroup *tgt;
	struct vcap_stream_iter iter;
	int idx, res, actfield_count;
	u32 *actstream;
	u8 value[16];
	bool no_bits;

	actstream = admin->cache.actionstream;
	res = vcap_find_actionstream_actionset(vctrl, vt, actstream, 0);
	if (res < 0) {
		pr_err("%s:%d: could not find valid actionset: %d\n",
		       __func__, __LINE__, res);
		return -EINVAL;
	}
	actionset = res;
	out->prf(out->dst, "  actionset: %s\n",
		 vcap_actionset_name(vctrl, ri->data.actionset));
	out->prf(out->dst, "  actionset_sw: %d\n", ri->actionset_sw);
	out->prf(out->dst, "  actionset_sw_regs: %d\n", ri->actionset_sw_regs);
	actfield_count = vcap_actionfield_count(vctrl, vt, actionset);
	actionfield = vcap_actionfields(vctrl, vt, actionset);
	tgt = vcap_actionfield_typegroup(vctrl, vt, actionset);
	/* Start decoding the stream */
	for (idx = 0; idx < actfield_count; ++idx) {
		if (actionfield[idx].width <= 0)
			continue;
		/* Get the action */
		memset(value, 0, DIV_ROUND_UP(actionfield[idx].width, 8));
		vcap_iter_init(&iter, vctrl->vcaps[vt].act_width, tgt,
			       actionfield[idx].offset);
		vcap_decode_field(actstream, &iter, actionfield[idx].width,
				  value);
		/* Skip if no bits are set */
		no_bits = vcap_bitarray_zero(actionfield[idx].width, value);
		if (no_bits)
			continue;
		/* Later the action id will also be checked */
		vcap_debugfs_show_rule_actionfield(vctrl, out, idx, actionfield,
						   value);
	}
	return 0;
}

static void vcap_show_admin_rule(struct vcap_control *vctrl,
				 struct vcap_admin *admin,
				 struct vcap_output_print *out,
				 struct vcap_rule_internal *ri)
{
	ri->counter.value = admin->cache.counter;
	ri->counter.sticky = admin->cache.sticky;
	out->prf(out->dst,
		 "rule: %u, addr: [%d,%d], X%d, ctr[%d]: %d, hit: %d\n",
		 ri->data.id, ri->addr, ri->addr + ri->size - 1, ri->size,
		 ri->counter_id, ri->counter.value, ri->counter.sticky);
	out->prf(out->dst, "  chain_id: %d\n", ri->data.vcap_chain_id);
	out->prf(out->dst, "  user: %d\n", ri->data.user);
	out->prf(out->dst, "  priority: %d\n", ri->data.priority);
	vcap_debugfs_show_rule_keyset(ri, out);
	vcap_debugfs_show_rule_actionset(ri, out);
}

static void vcap_show_admin_info(struct vcap_control *vctrl,
				 struct vcap_admin *admin,
				 struct vcap_output_print *out)
{
	const struct vcap_info *vcap = &vctrl->vcaps[admin->vtype];

	out->prf(out->dst, "name: %s\n", vcap->name);
	out->prf(out->dst, "rows: %d\n", vcap->rows);
	out->prf(out->dst, "sw_count: %d\n", vcap->sw_count);
	out->prf(out->dst, "sw_width: %d\n", vcap->sw_width);
	out->prf(out->dst, "sticky_width: %d\n", vcap->sticky_width);
	out->prf(out->dst, "act_width: %d\n", vcap->act_width);
	out->prf(out->dst, "default_cnt: %d\n", vcap->default_cnt);
	out->prf(out->dst, "require_cnt_dis: %d\n", vcap->require_cnt_dis);
	out->prf(out->dst, "version: %d\n", vcap->version);
	out->prf(out->dst, "vtype: %d\n", admin->vtype);
	out->prf(out->dst, "vinst: %d\n", admin->vinst);
	out->prf(out->dst, "first_cid: %d\n", admin->first_cid);
	out->prf(out->dst, "last_cid: %d\n", admin->last_cid);
	out->prf(out->dst, "lookups: %d\n", admin->lookups);
	out->prf(out->dst, "first_valid_addr: %d\n", admin->first_valid_addr);
	out->prf(out->dst, "last_valid_addr: %d\n", admin->last_valid_addr);
	out->prf(out->dst, "last_used_addr: %d\n", admin->last_used_addr);
}

static int vcap_show_admin(struct vcap_control *vctrl,
			   struct vcap_admin *admin,
			   struct vcap_output_print *out)
{
	struct vcap_rule_internal *elem, *ri;
	int ret = 0;

	vcap_show_admin_info(vctrl, admin, out);
	mutex_lock(&admin->lock);
	list_for_each_entry(elem, &admin->rules, list) {
		ri = vcap_dup_rule(elem);
		if (IS_ERR(ri))
			goto free_rule;
		/* Read data from VCAP */
		ret = vcap_read_rule(ri);
		if (ret)
			goto free_rule;
		out->prf(out->dst, "\n");
		vcap_show_admin_rule(vctrl, admin, out, ri);
free_rule:
		vcap_free_rule((struct vcap_rule *)ri);
	}
	mutex_unlock(&admin->lock);
	return ret;
}

static int vcap_show_admin_raw(struct vcap_control *vctrl,
			       struct vcap_admin *admin,
			       struct vcap_output_print *out)
{
	enum vcap_type vt = admin->vtype;
	struct vcap_rule_internal *ri;
	const struct vcap_set *info;
	int keyset;
	int addr;
	int ret;

	if (list_empty(&admin->rules))
		return 0;

	ret = vcap_api_check(vctrl);
	if (ret)
		return ret;

	ri = list_first_entry(&admin->rules, struct vcap_rule_internal, list);

	/* Go from higher to lower addresses searching for a keyset */
	for (addr = admin->last_valid_addr; addr >= admin->first_valid_addr;
	     --addr) {
		keyset = vcap_addr_keyset(vctrl, ri->ndev, admin,  addr);
		if (keyset < 0)
			continue;
		info = vcap_keyfieldset(vctrl, vt, keyset);
		if (!info)
			continue;
		if (addr % info->sw_per_item)
			pr_info("addr: %d X%d error rule, keyset: %s\n",
				addr,
				info->sw_per_item,
				vcap_keyset_name(vctrl, keyset));
		else
			out->prf(out->dst, "  addr: %d, X%d rule, keyset: %s\n",
			   addr,
			   info->sw_per_item,
			   vcap_keyset_name(vctrl, keyset));
	}
	return 0;
}

/* Show the port configuration and status */
static int vcap_port_debugfs_show(struct seq_file *m, void *unused)
{
	struct vcap_port_debugfs_info *info = m->private;
	struct vcap_admin *admin;
	struct vcap_output_print out = {
		.prf = (void *)seq_printf,
		.dst = m,
	};

	list_for_each_entry(admin, &info->vctrl->list, list) {
		if (admin->vinst)
			continue;
		info->vctrl->ops->port_info(info->ndev, admin, &out);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vcap_port_debugfs);

void vcap_port_debugfs(struct device *dev, struct dentry *parent,
		       struct vcap_control *vctrl,
		       struct net_device *ndev)
{
	struct vcap_port_debugfs_info *info;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	info->vctrl = vctrl;
	info->ndev = ndev;
	debugfs_create_file(netdev_name(ndev), 0444, parent, info,
			    &vcap_port_debugfs_fops);
}
EXPORT_SYMBOL_GPL(vcap_port_debugfs);

/* Show the full VCAP instance data (rules with all fields) */
static int vcap_debugfs_show(struct seq_file *m, void *unused)
{
	struct vcap_admin_debugfs_info *info = m->private;
	struct vcap_output_print out = {
		.prf = (void *)seq_printf,
		.dst = m,
	};

	return vcap_show_admin(info->vctrl, info->admin, &out);
}
DEFINE_SHOW_ATTRIBUTE(vcap_debugfs);

/* Show the raw VCAP instance data (rules with address info) */
static int vcap_raw_debugfs_show(struct seq_file *m, void *unused)
{
	struct vcap_admin_debugfs_info *info = m->private;
	struct vcap_output_print out = {
		.prf = (void *)seq_printf,
		.dst = m,
	};

	return vcap_show_admin_raw(info->vctrl, info->admin, &out);
}
DEFINE_SHOW_ATTRIBUTE(vcap_raw_debugfs);

struct dentry *vcap_debugfs(struct device *dev, struct dentry *parent,
			    struct vcap_control *vctrl)
{
	struct vcap_admin_debugfs_info *info;
	struct vcap_admin *admin;
	struct dentry *dir;
	char name[50];

	dir = debugfs_create_dir("vcaps", parent);
	if (PTR_ERR_OR_ZERO(dir))
		return NULL;
	list_for_each_entry(admin, &vctrl->list, list) {
		sprintf(name, "raw_%s_%d", vctrl->vcaps[admin->vtype].name,
			admin->vinst);
		info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
		if (!info)
			return NULL;
		info->vctrl = vctrl;
		info->admin = admin;
		debugfs_create_file(name, 0444, dir, info,
				    &vcap_raw_debugfs_fops);
		sprintf(name, "%s_%d", vctrl->vcaps[admin->vtype].name,
			admin->vinst);
		debugfs_create_file(name, 0444, dir, info, &vcap_debugfs_fops);
	}
	return dir;
}
EXPORT_SYMBOL_GPL(vcap_debugfs);

#ifdef CONFIG_VCAP_KUNIT_TEST
#include "vcap_api_debugfs_kunit.c"
#endif
