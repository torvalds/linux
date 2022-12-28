// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/types.h>

#include "vcap_api_private.h"

static int keyfield_size_table[] = {
	[VCAP_FIELD_BIT]  = sizeof(struct vcap_u1_key),
	[VCAP_FIELD_U32]  = sizeof(struct vcap_u32_key),
	[VCAP_FIELD_U48]  = sizeof(struct vcap_u48_key),
	[VCAP_FIELD_U56]  = sizeof(struct vcap_u56_key),
	[VCAP_FIELD_U64]  = sizeof(struct vcap_u64_key),
	[VCAP_FIELD_U72]  = sizeof(struct vcap_u72_key),
	[VCAP_FIELD_U112] = sizeof(struct vcap_u112_key),
	[VCAP_FIELD_U128] = sizeof(struct vcap_u128_key),
};

static int actionfield_size_table[] = {
	[VCAP_FIELD_BIT]  = sizeof(struct vcap_u1_action),
	[VCAP_FIELD_U32]  = sizeof(struct vcap_u32_action),
	[VCAP_FIELD_U48]  = sizeof(struct vcap_u48_action),
	[VCAP_FIELD_U56]  = sizeof(struct vcap_u56_action),
	[VCAP_FIELD_U64]  = sizeof(struct vcap_u64_action),
	[VCAP_FIELD_U72]  = sizeof(struct vcap_u72_action),
	[VCAP_FIELD_U112] = sizeof(struct vcap_u112_action),
	[VCAP_FIELD_U128] = sizeof(struct vcap_u128_action),
};

/* Moving a rule in the VCAP address space */
struct vcap_rule_move {
	int addr; /* address to move */
	int offset; /* change in address */
	int count; /* blocksize of addresses to move */
};

/* Stores the filter cookie that enabled the port */
struct vcap_enabled_port {
	struct list_head list; /* for insertion in enabled ports list */
	struct net_device *ndev;  /* the enabled port */
	unsigned long cookie; /* filter that enabled the port */
};

void vcap_iter_set(struct vcap_stream_iter *itr, int sw_width,
		   const struct vcap_typegroup *tg, u32 offset)
{
	memset(itr, 0, sizeof(*itr));
	itr->offset = offset;
	itr->sw_width = sw_width;
	itr->regs_per_sw = DIV_ROUND_UP(sw_width, 32);
	itr->tg = tg;
}

static void vcap_iter_skip_tg(struct vcap_stream_iter *itr)
{
	/* Compensate the field offset for preceding typegroups.
	 * A typegroup table ends with an all-zero terminator.
	 */
	while (itr->tg->width && itr->offset >= itr->tg->offset) {
		itr->offset += itr->tg->width;
		itr->tg++; /* next typegroup */
	}
}

void vcap_iter_update(struct vcap_stream_iter *itr)
{
	int sw_idx, sw_bitpos;

	/* Calculate the subword index and bitposition for current bit */
	sw_idx = itr->offset / itr->sw_width;
	sw_bitpos = itr->offset % itr->sw_width;
	/* Calculate the register index and bitposition for current bit */
	itr->reg_idx = (sw_idx * itr->regs_per_sw) + (sw_bitpos / 32);
	itr->reg_bitpos = sw_bitpos % 32;
}

void vcap_iter_init(struct vcap_stream_iter *itr, int sw_width,
		    const struct vcap_typegroup *tg, u32 offset)
{
	vcap_iter_set(itr, sw_width, tg, offset);
	vcap_iter_skip_tg(itr);
	vcap_iter_update(itr);
}

void vcap_iter_next(struct vcap_stream_iter *itr)
{
	itr->offset++;
	vcap_iter_skip_tg(itr);
	vcap_iter_update(itr);
}

static void vcap_set_bit(u32 *stream, struct vcap_stream_iter *itr, bool value)
{
	u32 mask = BIT(itr->reg_bitpos);
	u32 *p = &stream[itr->reg_idx];

	if (value)
		*p |= mask;
	else
		*p &= ~mask;
}

static void vcap_encode_bit(u32 *stream, struct vcap_stream_iter *itr, bool val)
{
	/* When intersected by a type group field, stream the type group bits
	 * before continuing with the value bit
	 */
	while (itr->tg->width &&
	       itr->offset >= itr->tg->offset &&
	       itr->offset < itr->tg->offset + itr->tg->width) {
		int tg_bitpos = itr->tg->offset - itr->offset;

		vcap_set_bit(stream, itr, (itr->tg->value >> tg_bitpos) & 0x1);
		itr->offset++;
		vcap_iter_update(itr);
	}
	vcap_set_bit(stream, itr, val);
}

static void vcap_encode_field(u32 *stream, struct vcap_stream_iter *itr,
			      int width, const u8 *value)
{
	int idx;

	/* Loop over the field value bits and add the value bits one by one to
	 * the output stream.
	 */
	for (idx = 0; idx < width; idx++) {
		u8 bidx = idx & GENMASK(2, 0);

		/* Encode one field value bit */
		vcap_encode_bit(stream, itr, (value[idx / 8] >> bidx) & 0x1);
		vcap_iter_next(itr);
	}
}

static void vcap_encode_typegroups(u32 *stream, int sw_width,
				   const struct vcap_typegroup *tg,
				   bool mask)
{
	struct vcap_stream_iter iter;
	int idx;

	/* Mask bits must be set to zeros (inverted later when writing to the
	 * mask cache register), so that the mask typegroup bits consist of
	 * match-1 or match-0, or both
	 */
	vcap_iter_set(&iter, sw_width, tg, 0);
	while (iter.tg->width) {
		/* Set position to current typegroup bit */
		iter.offset = iter.tg->offset;
		vcap_iter_update(&iter);
		for (idx = 0; idx < iter.tg->width; idx++) {
			/* Iterate over current typegroup bits. Mask typegroup
			 * bits are always set
			 */
			if (mask)
				vcap_set_bit(stream, &iter, 0x1);
			else
				vcap_set_bit(stream, &iter,
					     (iter.tg->value >> idx) & 0x1);
			iter.offset++;
			vcap_iter_update(&iter);
		}
		iter.tg++; /* next typegroup */
	}
}

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

	return (value & mask) == (info->type_id & mask);
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

/* Verify that the typegroup information, subword count, keyset and type id
 * are in sync and correct, return the list of matchin keysets
 */
int
vcap_find_keystream_keysets(struct vcap_control *vctrl,
			    enum vcap_type vt,
			    u32 *keystream,
			    u32 *mskstream,
			    bool mask, int sw_max,
			    struct vcap_keyset_list *kslist)
{
	const struct vcap_set *keyfield_set;
	int sw_count, idx;

	sw_count = vcap_find_keystream_typegroup_sw(vctrl, vt, keystream, mask,
						    sw_max);
	if (sw_count < 0)
		return sw_count;

	keyfield_set = vctrl->vcaps[vt].keyfield_set;
	for (idx = 0; idx < vctrl->vcaps[vt].keyfield_set_size; ++idx) {
		if (keyfield_set[idx].sw_per_item != sw_count)
			continue;

		if (vcap_verify_keystream_keyset(vctrl, vt, keystream,
						 mskstream, idx))
			vcap_keyset_list_add(kslist, idx);
	}
	if (kslist->cnt > 0)
		return 0;
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(vcap_find_keystream_keysets);

/* Read key data from a VCAP address and discover if there are any rule keysets
 * here
 */
int vcap_addr_keysets(struct vcap_control *vctrl,
		      struct net_device *ndev,
		      struct vcap_admin *admin,
		      int addr,
		      struct vcap_keyset_list *kslist)
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
	/* Decode and locate the keysets */
	return vcap_find_keystream_keysets(vctrl, vt, admin->cache.keystream,
					   admin->cache.maskstream, false, 0,
					   kslist);
}
EXPORT_SYMBOL_GPL(vcap_addr_keysets);

/* Return the list of keyfields for the keyset */
const struct vcap_field *vcap_keyfields(struct vcap_control *vctrl,
					enum vcap_type vt,
					enum vcap_keyfield_set keyset)
{
	/* Check that the keyset exists in the vcap keyset list */
	if (keyset >= vctrl->vcaps[vt].keyfield_set_size)
		return NULL;
	return vctrl->vcaps[vt].keyfield_set_map[keyset];
}

/* Return the keyset information for the keyset */
const struct vcap_set *vcap_keyfieldset(struct vcap_control *vctrl,
					enum vcap_type vt,
					enum vcap_keyfield_set keyset)
{
	const struct vcap_set *kset;

	/* Check that the keyset exists in the vcap keyset list */
	if (keyset >= vctrl->vcaps[vt].keyfield_set_size)
		return NULL;
	kset = &vctrl->vcaps[vt].keyfield_set[keyset];
	if (kset->sw_per_item == 0 || kset->sw_per_item > vctrl->vcaps[vt].sw_count)
		return NULL;
	return kset;
}
EXPORT_SYMBOL_GPL(vcap_keyfieldset);

/* Return the typegroup table for the matching keyset (using subword size) */
const struct vcap_typegroup *
vcap_keyfield_typegroup(struct vcap_control *vctrl,
			enum vcap_type vt, enum vcap_keyfield_set keyset)
{
	const struct vcap_set *kset = vcap_keyfieldset(vctrl, vt, keyset);

	/* Check that the keyset is valid */
	if (!kset)
		return NULL;
	return vctrl->vcaps[vt].keyfield_set_typegroups[kset->sw_per_item];
}

/* Return the number of keyfields in the keyset */
int vcap_keyfield_count(struct vcap_control *vctrl,
			enum vcap_type vt, enum vcap_keyfield_set keyset)
{
	/* Check that the keyset exists in the vcap keyset list */
	if (keyset >= vctrl->vcaps[vt].keyfield_set_size)
		return 0;
	return vctrl->vcaps[vt].keyfield_set_map_size[keyset];
}

static void vcap_encode_keyfield(struct vcap_rule_internal *ri,
				 const struct vcap_client_keyfield *kf,
				 const struct vcap_field *rf,
				 const struct vcap_typegroup *tgt)
{
	int sw_width = ri->vctrl->vcaps[ri->admin->vtype].sw_width;
	struct vcap_cache_data *cache = &ri->admin->cache;
	struct vcap_stream_iter iter;
	const u8 *value, *mask;

	/* Encode the fields for the key and the mask in their respective
	 * streams, respecting the subword width.
	 */
	switch (kf->ctrl.type) {
	case VCAP_FIELD_BIT:
		value = &kf->data.u1.value;
		mask = &kf->data.u1.mask;
		break;
	case VCAP_FIELD_U32:
		value = (const u8 *)&kf->data.u32.value;
		mask = (const u8 *)&kf->data.u32.mask;
		break;
	case VCAP_FIELD_U48:
		value = kf->data.u48.value;
		mask = kf->data.u48.mask;
		break;
	case VCAP_FIELD_U56:
		value = kf->data.u56.value;
		mask = kf->data.u56.mask;
		break;
	case VCAP_FIELD_U64:
		value = kf->data.u64.value;
		mask = kf->data.u64.mask;
		break;
	case VCAP_FIELD_U72:
		value = kf->data.u72.value;
		mask = kf->data.u72.mask;
		break;
	case VCAP_FIELD_U112:
		value = kf->data.u112.value;
		mask = kf->data.u112.mask;
		break;
	case VCAP_FIELD_U128:
		value = kf->data.u128.value;
		mask = kf->data.u128.mask;
		break;
	}
	vcap_iter_init(&iter, sw_width, tgt, rf->offset);
	vcap_encode_field(cache->keystream, &iter, rf->width, value);
	vcap_iter_init(&iter, sw_width, tgt, rf->offset);
	vcap_encode_field(cache->maskstream, &iter, rf->width, mask);
}

static void vcap_encode_keyfield_typegroups(struct vcap_control *vctrl,
					    struct vcap_rule_internal *ri,
					    const struct vcap_typegroup *tgt)
{
	int sw_width = vctrl->vcaps[ri->admin->vtype].sw_width;
	struct vcap_cache_data *cache = &ri->admin->cache;

	/* Encode the typegroup bits for the key and the mask in their streams,
	 * respecting the subword width.
	 */
	vcap_encode_typegroups(cache->keystream, sw_width, tgt, false);
	vcap_encode_typegroups(cache->maskstream, sw_width, tgt, true);
}

static int vcap_encode_rule_keyset(struct vcap_rule_internal *ri)
{
	const struct vcap_client_keyfield *ckf;
	const struct vcap_typegroup *tg_table;
	const struct vcap_field *kf_table;
	int keyset_size;

	/* Get a valid set of fields for the specific keyset */
	kf_table = vcap_keyfields(ri->vctrl, ri->admin->vtype, ri->data.keyset);
	if (!kf_table) {
		pr_err("%s:%d: no fields available for this keyset: %d\n",
		       __func__, __LINE__, ri->data.keyset);
		return -EINVAL;
	}
	/* Get a valid typegroup for the specific keyset */
	tg_table = vcap_keyfield_typegroup(ri->vctrl, ri->admin->vtype,
					   ri->data.keyset);
	if (!tg_table) {
		pr_err("%s:%d: no typegroups available for this keyset: %d\n",
		       __func__, __LINE__, ri->data.keyset);
		return -EINVAL;
	}
	/* Get a valid size for the specific keyset */
	keyset_size = vcap_keyfield_count(ri->vctrl, ri->admin->vtype,
					  ri->data.keyset);
	if (keyset_size == 0) {
		pr_err("%s:%d: zero field count for this keyset: %d\n",
		       __func__, __LINE__, ri->data.keyset);
		return -EINVAL;
	}
	/* Iterate over the keyfields (key, mask) in the rule
	 * and encode these bits
	 */
	if (list_empty(&ri->data.keyfields)) {
		pr_err("%s:%d: no keyfields in the rule\n", __func__, __LINE__);
		return -EINVAL;
	}
	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list) {
		/* Check that the client entry exists in the keyset */
		if (ckf->ctrl.key >= keyset_size) {
			pr_err("%s:%d: key %d is not in vcap\n",
			       __func__, __LINE__, ckf->ctrl.key);
			return -EINVAL;
		}
		vcap_encode_keyfield(ri, ckf, &kf_table[ckf->ctrl.key], tg_table);
	}
	/* Add typegroup bits to the key/mask bitstreams */
	vcap_encode_keyfield_typegroups(ri->vctrl, ri, tg_table);
	return 0;
}

/* Return the list of actionfields for the actionset */
const struct vcap_field *
vcap_actionfields(struct vcap_control *vctrl,
		  enum vcap_type vt, enum vcap_actionfield_set actionset)
{
	/* Check that the actionset exists in the vcap actionset list */
	if (actionset >= vctrl->vcaps[vt].actionfield_set_size)
		return NULL;
	return vctrl->vcaps[vt].actionfield_set_map[actionset];
}

const struct vcap_set *
vcap_actionfieldset(struct vcap_control *vctrl,
		    enum vcap_type vt, enum vcap_actionfield_set actionset)
{
	const struct vcap_set *aset;

	/* Check that the actionset exists in the vcap actionset list */
	if (actionset >= vctrl->vcaps[vt].actionfield_set_size)
		return NULL;
	aset = &vctrl->vcaps[vt].actionfield_set[actionset];
	if (aset->sw_per_item == 0 || aset->sw_per_item > vctrl->vcaps[vt].sw_count)
		return NULL;
	return aset;
}

/* Return the typegroup table for the matching actionset (using subword size) */
const struct vcap_typegroup *
vcap_actionfield_typegroup(struct vcap_control *vctrl,
			   enum vcap_type vt, enum vcap_actionfield_set actionset)
{
	const struct vcap_set *aset = vcap_actionfieldset(vctrl, vt, actionset);

	/* Check that the actionset is valid */
	if (!aset)
		return NULL;
	return vctrl->vcaps[vt].actionfield_set_typegroups[aset->sw_per_item];
}

/* Return the number of actionfields in the actionset */
int vcap_actionfield_count(struct vcap_control *vctrl,
			   enum vcap_type vt,
			   enum vcap_actionfield_set actionset)
{
	/* Check that the actionset exists in the vcap actionset list */
	if (actionset >= vctrl->vcaps[vt].actionfield_set_size)
		return 0;
	return vctrl->vcaps[vt].actionfield_set_map_size[actionset];
}

static void vcap_encode_actionfield(struct vcap_rule_internal *ri,
				    const struct vcap_client_actionfield *af,
				    const struct vcap_field *rf,
				    const struct vcap_typegroup *tgt)
{
	int act_width = ri->vctrl->vcaps[ri->admin->vtype].act_width;

	struct vcap_cache_data *cache = &ri->admin->cache;
	struct vcap_stream_iter iter;
	const u8 *value;

	/* Encode the action field in the stream, respecting the subword width */
	switch (af->ctrl.type) {
	case VCAP_FIELD_BIT:
		value = &af->data.u1.value;
		break;
	case VCAP_FIELD_U32:
		value = (const u8 *)&af->data.u32.value;
		break;
	case VCAP_FIELD_U48:
		value = af->data.u48.value;
		break;
	case VCAP_FIELD_U56:
		value = af->data.u56.value;
		break;
	case VCAP_FIELD_U64:
		value = af->data.u64.value;
		break;
	case VCAP_FIELD_U72:
		value = af->data.u72.value;
		break;
	case VCAP_FIELD_U112:
		value = af->data.u112.value;
		break;
	case VCAP_FIELD_U128:
		value = af->data.u128.value;
		break;
	}
	vcap_iter_init(&iter, act_width, tgt, rf->offset);
	vcap_encode_field(cache->actionstream, &iter, rf->width, value);
}

static void vcap_encode_actionfield_typegroups(struct vcap_rule_internal *ri,
					       const struct vcap_typegroup *tgt)
{
	int sw_width = ri->vctrl->vcaps[ri->admin->vtype].act_width;
	struct vcap_cache_data *cache = &ri->admin->cache;

	/* Encode the typegroup bits for the actionstream respecting the subword
	 * width.
	 */
	vcap_encode_typegroups(cache->actionstream, sw_width, tgt, false);
}

static int vcap_encode_rule_actionset(struct vcap_rule_internal *ri)
{
	const struct vcap_client_actionfield *caf;
	const struct vcap_typegroup *tg_table;
	const struct vcap_field *af_table;
	int actionset_size;

	/* Get a valid set of actionset fields for the specific actionset */
	af_table = vcap_actionfields(ri->vctrl, ri->admin->vtype,
				     ri->data.actionset);
	if (!af_table) {
		pr_err("%s:%d: no fields available for this actionset: %d\n",
		       __func__, __LINE__, ri->data.actionset);
		return -EINVAL;
	}
	/* Get a valid typegroup for the specific actionset */
	tg_table = vcap_actionfield_typegroup(ri->vctrl, ri->admin->vtype,
					      ri->data.actionset);
	if (!tg_table) {
		pr_err("%s:%d: no typegroups available for this actionset: %d\n",
		       __func__, __LINE__, ri->data.actionset);
		return -EINVAL;
	}
	/* Get a valid actionset size for the specific actionset */
	actionset_size = vcap_actionfield_count(ri->vctrl, ri->admin->vtype,
						ri->data.actionset);
	if (actionset_size == 0) {
		pr_err("%s:%d: zero field count for this actionset: %d\n",
		       __func__, __LINE__, ri->data.actionset);
		return -EINVAL;
	}
	/* Iterate over the actionfields in the rule
	 * and encode these bits
	 */
	if (list_empty(&ri->data.actionfields))
		pr_warn("%s:%d: no actionfields in the rule\n",
			__func__, __LINE__);
	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list) {
		/* Check that the client action exists in the actionset */
		if (caf->ctrl.action >= actionset_size) {
			pr_err("%s:%d: action %d is not in vcap\n",
			       __func__, __LINE__, caf->ctrl.action);
			return -EINVAL;
		}
		vcap_encode_actionfield(ri, caf, &af_table[caf->ctrl.action],
					tg_table);
	}
	/* Add typegroup bits to the entry bitstreams */
	vcap_encode_actionfield_typegroups(ri, tg_table);
	return 0;
}

static int vcap_encode_rule(struct vcap_rule_internal *ri)
{
	int err;

	err = vcap_encode_rule_keyset(ri);
	if (err)
		return err;
	err = vcap_encode_rule_actionset(ri);
	if (err)
		return err;
	return 0;
}

int vcap_api_check(struct vcap_control *ctrl)
{
	if (!ctrl) {
		pr_err("%s:%d: vcap control is missing\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (!ctrl->ops || !ctrl->ops->validate_keyset ||
	    !ctrl->ops->add_default_fields || !ctrl->ops->cache_erase ||
	    !ctrl->ops->cache_write || !ctrl->ops->cache_read ||
	    !ctrl->ops->init || !ctrl->ops->update || !ctrl->ops->move ||
	    !ctrl->ops->port_info || !ctrl->ops->enable) {
		pr_err("%s:%d: client operations are missing\n",
		       __func__, __LINE__);
		return -ENOENT;
	}
	return 0;
}

void vcap_erase_cache(struct vcap_rule_internal *ri)
{
	ri->vctrl->ops->cache_erase(ri->admin);
}

/* Update the keyset for the rule */
int vcap_set_rule_set_keyset(struct vcap_rule *rule,
			     enum vcap_keyfield_set keyset)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	const struct vcap_set *kset;
	int sw_width;

	kset = vcap_keyfieldset(ri->vctrl, ri->admin->vtype, keyset);
	/* Check that the keyset is valid */
	if (!kset)
		return -EINVAL;
	ri->keyset_sw = kset->sw_per_item;
	sw_width = ri->vctrl->vcaps[ri->admin->vtype].sw_width;
	ri->keyset_sw_regs = DIV_ROUND_UP(sw_width, 32);
	ri->data.keyset = keyset;
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_set_rule_set_keyset);

/* Update the actionset for the rule */
int vcap_set_rule_set_actionset(struct vcap_rule *rule,
				enum vcap_actionfield_set actionset)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	const struct vcap_set *aset;
	int act_width;

	aset = vcap_actionfieldset(ri->vctrl, ri->admin->vtype, actionset);
	/* Check that the actionset is valid */
	if (!aset)
		return -EINVAL;
	ri->actionset_sw = aset->sw_per_item;
	act_width = ri->vctrl->vcaps[ri->admin->vtype].act_width;
	ri->actionset_sw_regs = DIV_ROUND_UP(act_width, 32);
	ri->data.actionset = actionset;
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_set_rule_set_actionset);

/* Find a rule with a provided rule id */
static struct vcap_rule_internal *vcap_lookup_rule(struct vcap_control *vctrl,
						   u32 id)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(ri, &admin->rules, list)
			if (ri->data.id == id)
				return ri;
	return NULL;
}

/* Find a rule id with a provided cookie */
int vcap_lookup_rule_by_cookie(struct vcap_control *vctrl, u64 cookie)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(ri, &admin->rules, list)
			if (ri->data.cookie == cookie)
				return ri->data.id;
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(vcap_lookup_rule_by_cookie);

/* Make a shallow copy of the rule without the fields */
struct vcap_rule_internal *vcap_dup_rule(struct vcap_rule_internal *ri)
{
	struct vcap_rule_internal *duprule;

	/* Allocate the client part */
	duprule = kzalloc(sizeof(*duprule), GFP_KERNEL);
	if (!duprule)
		return ERR_PTR(-ENOMEM);
	*duprule = *ri;
	/* Not inserted in the VCAP */
	INIT_LIST_HEAD(&duprule->list);
	/* No elements in these lists */
	INIT_LIST_HEAD(&duprule->data.keyfields);
	INIT_LIST_HEAD(&duprule->data.actionfields);
	return duprule;
}

static void vcap_apply_width(u8 *dst, int width, int bytes)
{
	u8 bmask;
	int idx;

	for (idx = 0; idx < bytes; idx++) {
		if (width > 0)
			if (width < 8)
				bmask = (1 << width) - 1;
			else
				bmask = ~0;
		else
			bmask = 0;
		dst[idx] &= bmask;
		width -= 8;
	}
}

static void vcap_copy_from_w32be(u8 *dst, u8 *src, int size, int width)
{
	int idx, ridx, wstart, nidx;
	int tail_bytes = (((size + 4) >> 2) << 2) - size;

	for (idx = 0, ridx = size - 1; idx < size; ++idx, --ridx) {
		wstart = (idx >> 2) << 2;
		nidx = wstart + 3 - (idx & 0x3);
		if (nidx >= size)
			nidx -= tail_bytes;
		dst[nidx] = src[ridx];
	}

	vcap_apply_width(dst, width, size);
}

static void vcap_copy_action_bit_field(struct vcap_u1_action *field, u8 *value)
{
	field->value = (*value) & 0x1;
}

static void vcap_copy_limited_actionfield(u8 *dstvalue, u8 *srcvalue,
					  int width, int bytes)
{
	memcpy(dstvalue, srcvalue, bytes);
	vcap_apply_width(dstvalue, width, bytes);
}

static void vcap_copy_to_client_actionfield(struct vcap_rule_internal *ri,
					    struct vcap_client_actionfield *field,
					    u8 *value, u16 width)
{
	int field_size = actionfield_size_table[field->ctrl.type];

	if (ri->admin->w32be) {
		switch (field->ctrl.type) {
		case VCAP_FIELD_BIT:
			vcap_copy_action_bit_field(&field->data.u1, value);
			break;
		case VCAP_FIELD_U32:
			vcap_copy_limited_actionfield((u8 *)&field->data.u32.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U48:
			vcap_copy_from_w32be(field->data.u48.value, value,
					     field_size, width);
			break;
		case VCAP_FIELD_U56:
			vcap_copy_from_w32be(field->data.u56.value, value,
					     field_size, width);
			break;
		case VCAP_FIELD_U64:
			vcap_copy_from_w32be(field->data.u64.value, value,
					     field_size, width);
			break;
		case VCAP_FIELD_U72:
			vcap_copy_from_w32be(field->data.u72.value, value,
					     field_size, width);
			break;
		case VCAP_FIELD_U112:
			vcap_copy_from_w32be(field->data.u112.value, value,
					     field_size, width);
			break;
		case VCAP_FIELD_U128:
			vcap_copy_from_w32be(field->data.u128.value, value,
					     field_size, width);
			break;
		};
	} else {
		switch (field->ctrl.type) {
		case VCAP_FIELD_BIT:
			vcap_copy_action_bit_field(&field->data.u1, value);
			break;
		case VCAP_FIELD_U32:
			vcap_copy_limited_actionfield((u8 *)&field->data.u32.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U48:
			vcap_copy_limited_actionfield(field->data.u48.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U56:
			vcap_copy_limited_actionfield(field->data.u56.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U64:
			vcap_copy_limited_actionfield(field->data.u64.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U72:
			vcap_copy_limited_actionfield(field->data.u72.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U112:
			vcap_copy_limited_actionfield(field->data.u112.value,
						      value,
						      width, field_size);
			break;
		case VCAP_FIELD_U128:
			vcap_copy_limited_actionfield(field->data.u128.value,
						      value,
						      width, field_size);
			break;
		};
	}
}

static void vcap_copy_key_bit_field(struct vcap_u1_key *field,
				    u8 *value, u8 *mask)
{
	field->value = (*value) & 0x1;
	field->mask = (*mask) & 0x1;
}

static void vcap_copy_limited_keyfield(u8 *dstvalue, u8 *dstmask,
				       u8 *srcvalue, u8 *srcmask,
				       int width, int bytes)
{
	memcpy(dstvalue, srcvalue, bytes);
	vcap_apply_width(dstvalue, width, bytes);
	memcpy(dstmask, srcmask, bytes);
	vcap_apply_width(dstmask, width, bytes);
}

static void vcap_copy_to_client_keyfield(struct vcap_rule_internal *ri,
					 struct vcap_client_keyfield *field,
					 u8 *value, u8 *mask, u16 width)
{
	int field_size = keyfield_size_table[field->ctrl.type] / 2;

	if (ri->admin->w32be) {
		switch (field->ctrl.type) {
		case VCAP_FIELD_BIT:
			vcap_copy_key_bit_field(&field->data.u1, value, mask);
			break;
		case VCAP_FIELD_U32:
			vcap_copy_limited_keyfield((u8 *)&field->data.u32.value,
						   (u8 *)&field->data.u32.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U48:
			vcap_copy_from_w32be(field->data.u48.value, value,
					     field_size, width);
			vcap_copy_from_w32be(field->data.u48.mask,  mask,
					     field_size, width);
			break;
		case VCAP_FIELD_U56:
			vcap_copy_from_w32be(field->data.u56.value, value,
					     field_size, width);
			vcap_copy_from_w32be(field->data.u56.mask,  mask,
					     field_size, width);
			break;
		case VCAP_FIELD_U64:
			vcap_copy_from_w32be(field->data.u64.value, value,
					     field_size, width);
			vcap_copy_from_w32be(field->data.u64.mask,  mask,
					     field_size, width);
			break;
		case VCAP_FIELD_U72:
			vcap_copy_from_w32be(field->data.u72.value, value,
					     field_size, width);
			vcap_copy_from_w32be(field->data.u72.mask,  mask,
					     field_size, width);
			break;
		case VCAP_FIELD_U112:
			vcap_copy_from_w32be(field->data.u112.value, value,
					     field_size, width);
			vcap_copy_from_w32be(field->data.u112.mask,  mask,
					     field_size, width);
			break;
		case VCAP_FIELD_U128:
			vcap_copy_from_w32be(field->data.u128.value, value,
					     field_size, width);
			vcap_copy_from_w32be(field->data.u128.mask,  mask,
					     field_size, width);
			break;
		};
	} else {
		switch (field->ctrl.type) {
		case VCAP_FIELD_BIT:
			vcap_copy_key_bit_field(&field->data.u1, value, mask);
			break;
		case VCAP_FIELD_U32:
			vcap_copy_limited_keyfield((u8 *)&field->data.u32.value,
						   (u8 *)&field->data.u32.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U48:
			vcap_copy_limited_keyfield(field->data.u48.value,
						   field->data.u48.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U56:
			vcap_copy_limited_keyfield(field->data.u56.value,
						   field->data.u56.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U64:
			vcap_copy_limited_keyfield(field->data.u64.value,
						   field->data.u64.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U72:
			vcap_copy_limited_keyfield(field->data.u72.value,
						   field->data.u72.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U112:
			vcap_copy_limited_keyfield(field->data.u112.value,
						   field->data.u112.mask,
						   value, mask,
						   width, field_size);
			break;
		case VCAP_FIELD_U128:
			vcap_copy_limited_keyfield(field->data.u128.value,
						   field->data.u128.mask,
						   value, mask,
						   width, field_size);
			break;
		};
	}
}

static void vcap_rule_alloc_keyfield(struct vcap_rule_internal *ri,
				     const struct vcap_field *keyfield,
				     enum vcap_key_field key,
				     u8 *value, u8 *mask)
{
	struct vcap_client_keyfield *field;

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return;
	INIT_LIST_HEAD(&field->ctrl.list);
	field->ctrl.key = key;
	field->ctrl.type = keyfield->type;
	vcap_copy_to_client_keyfield(ri, field, value, mask, keyfield->width);
	list_add_tail(&field->ctrl.list, &ri->data.keyfields);
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

/* Store action value in an element in a list for the client */
static void vcap_rule_alloc_actionfield(struct vcap_rule_internal *ri,
					const struct vcap_field *actionfield,
					enum vcap_action_field action,
					u8 *value)
{
	struct vcap_client_actionfield *field;

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return;
	INIT_LIST_HEAD(&field->ctrl.list);
	field->ctrl.action = action;
	field->ctrl.type = actionfield->type;
	vcap_copy_to_client_actionfield(ri, field, value, actionfield->width);
	list_add_tail(&field->ctrl.list, &ri->data.actionfields);
}

static int vcap_decode_actionset(struct vcap_rule_internal *ri)
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

	actstream = admin->cache.actionstream;
	res = vcap_find_actionstream_actionset(vctrl, vt, actstream, 0);
	if (res < 0) {
		pr_err("%s:%d: could not find valid actionset: %d\n",
		       __func__, __LINE__, res);
		return -EINVAL;
	}
	actionset = res;
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
		if (vcap_bitarray_zero(actionfield[idx].width, value))
			continue;
		vcap_rule_alloc_actionfield(ri, &actionfield[idx], idx, value);
		/* Later the action id will also be checked */
	}
	return vcap_set_rule_set_actionset((struct vcap_rule *)ri, actionset);
}

static int vcap_decode_keyset(struct vcap_rule_internal *ri)
{
	struct vcap_control *vctrl = ri->vctrl;
	struct vcap_stream_iter kiter, miter;
	struct vcap_admin *admin = ri->admin;
	enum vcap_keyfield_set keysets[10];
	const struct vcap_field *keyfield;
	enum vcap_type vt = admin->vtype;
	const struct vcap_typegroup *tgt;
	struct vcap_keyset_list matches;
	enum vcap_keyfield_set keyset;
	int idx, res, keyfield_count;
	u32 *maskstream;
	u32 *keystream;
	u8 value[16];
	u8 mask[16];

	keystream = admin->cache.keystream;
	maskstream = admin->cache.maskstream;
	matches.keysets = keysets;
	matches.cnt = 0;
	matches.max = ARRAY_SIZE(keysets);
	res = vcap_find_keystream_keysets(vctrl, vt, keystream, maskstream,
					  false, 0, &matches);
	if (res < 0) {
		pr_err("%s:%d: could not find valid keysets: %d\n",
		       __func__, __LINE__, res);
		return -EINVAL;
	}
	keyset = matches.keysets[0];
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
		vcap_rule_alloc_keyfield(ri, &keyfield[idx], idx, value, mask);
	}
	return vcap_set_rule_set_keyset((struct vcap_rule *)ri, keyset);
}

/* Read VCAP content into the VCAP cache */
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

/* Write VCAP cache content to the VCAP HW instance */
static int vcap_write_rule(struct vcap_rule_internal *ri)
{
	struct vcap_admin *admin = ri->admin;
	int sw_idx, ent_idx = 0, act_idx = 0;
	u32 addr = ri->addr;

	if (!ri->size || !ri->keyset_sw_regs || !ri->actionset_sw_regs) {
		pr_err("%s:%d: rule is empty\n", __func__, __LINE__);
		return -EINVAL;
	}
	/* Use the values in the streams to write the VCAP cache */
	for (sw_idx = 0; sw_idx < ri->size; sw_idx++, addr++) {
		ri->vctrl->ops->cache_write(ri->ndev, admin,
					    VCAP_SEL_ENTRY, ent_idx,
					    ri->keyset_sw_regs);
		ri->vctrl->ops->cache_write(ri->ndev, admin,
					    VCAP_SEL_ACTION, act_idx,
					    ri->actionset_sw_regs);
		ri->vctrl->ops->update(ri->ndev, admin, VCAP_CMD_WRITE,
				       VCAP_SEL_ALL, addr);
		ent_idx += ri->keyset_sw_regs;
		act_idx += ri->actionset_sw_regs;
	}
	return 0;
}

static int vcap_write_counter(struct vcap_rule_internal *ri,
			      struct vcap_counter *ctr)
{
	struct vcap_admin *admin = ri->admin;

	admin->cache.counter = ctr->value;
	admin->cache.sticky = ctr->sticky;
	ri->vctrl->ops->cache_write(ri->ndev, admin, VCAP_SEL_COUNTER,
				    ri->counter_id, 0);
	ri->vctrl->ops->update(ri->ndev, admin, VCAP_CMD_WRITE,
			       VCAP_SEL_COUNTER, ri->addr);
	return 0;
}

/* Convert a chain id to a VCAP lookup index */
int vcap_chain_id_to_lookup(struct vcap_admin *admin, int cur_cid)
{
	int lookup_first = admin->vinst * admin->lookups_per_instance;
	int lookup_last = lookup_first + admin->lookups_per_instance;
	int cid_next = admin->first_cid + VCAP_CID_LOOKUP_SIZE;
	int cid = admin->first_cid;
	int lookup;

	for (lookup = lookup_first; lookup < lookup_last; ++lookup,
	     cid += VCAP_CID_LOOKUP_SIZE, cid_next += VCAP_CID_LOOKUP_SIZE)
		if (cur_cid >= cid && cur_cid < cid_next)
			return lookup;
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_chain_id_to_lookup);

/* Lookup a vcap instance using chain id */
struct vcap_admin *vcap_find_admin(struct vcap_control *vctrl, int cid)
{
	struct vcap_admin *admin;

	if (vcap_api_check(vctrl))
		return NULL;

	list_for_each_entry(admin, &vctrl->list, list) {
		if (cid >= admin->first_cid && cid <= admin->last_cid)
			return admin;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(vcap_find_admin);

/* Is the next chain id in the following lookup, possible in another VCAP */
bool vcap_is_next_lookup(struct vcap_control *vctrl, int cur_cid, int next_cid)
{
	struct vcap_admin *admin, *next_admin;
	int lookup, next_lookup;

	/* The offset must be at least one lookup */
	if (next_cid < cur_cid + VCAP_CID_LOOKUP_SIZE)
		return false;

	if (vcap_api_check(vctrl))
		return false;

	admin = vcap_find_admin(vctrl, cur_cid);
	if (!admin)
		return false;

	/* If no VCAP contains the next chain, the next chain must be beyond
	 * the last chain in the current VCAP
	 */
	next_admin = vcap_find_admin(vctrl, next_cid);
	if (!next_admin)
		return next_cid > admin->last_cid;

	lookup = vcap_chain_id_to_lookup(admin, cur_cid);
	next_lookup = vcap_chain_id_to_lookup(next_admin, next_cid);

	/* Next lookup must be the following lookup */
	if (admin == next_admin || admin->vtype == next_admin->vtype)
		return next_lookup == lookup + 1;

	/* Must be the first lookup in the next VCAP instance */
	return next_lookup == 0;
}
EXPORT_SYMBOL_GPL(vcap_is_next_lookup);

/* Check if there is room for a new rule */
static int vcap_rule_space(struct vcap_admin *admin, int size)
{
	if (admin->last_used_addr - size < admin->first_valid_addr) {
		pr_err("%s:%d: No room for rule size: %u, %u\n",
		       __func__, __LINE__, size, admin->first_valid_addr);
		return -ENOSPC;
	}
	return 0;
}

/* Add the keyset typefield to the list of rule keyfields */
static int vcap_add_type_keyfield(struct vcap_rule *rule)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	enum vcap_keyfield_set keyset = rule->keyset;
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_field *fields;
	const struct vcap_set *kset;
	int ret = -EINVAL;

	kset = vcap_keyfieldset(ri->vctrl, vt, keyset);
	if (!kset)
		return ret;
	if (kset->type_id == (u8)-1)  /* No type field is needed */
		return 0;

	fields = vcap_keyfields(ri->vctrl, vt, keyset);
	if (!fields)
		return -EINVAL;
	if (fields[VCAP_KF_TYPE].width > 1) {
		ret = vcap_rule_add_key_u32(rule, VCAP_KF_TYPE,
					    kset->type_id, 0xff);
	} else {
		if (kset->type_id)
			ret = vcap_rule_add_key_bit(rule, VCAP_KF_TYPE,
						    VCAP_BIT_1);
		else
			ret = vcap_rule_add_key_bit(rule, VCAP_KF_TYPE,
						    VCAP_BIT_0);
	}
	return 0;
}

/* Add a keyset to a keyset list */
bool vcap_keyset_list_add(struct vcap_keyset_list *keysetlist,
			  enum vcap_keyfield_set keyset)
{
	int idx;

	if (keysetlist->cnt < keysetlist->max) {
		/* Avoid duplicates */
		for (idx = 0; idx < keysetlist->cnt; ++idx)
			if (keysetlist->keysets[idx] == keyset)
				return keysetlist->cnt < keysetlist->max;
		keysetlist->keysets[keysetlist->cnt++] = keyset;
	}
	return keysetlist->cnt < keysetlist->max;
}
EXPORT_SYMBOL_GPL(vcap_keyset_list_add);

/* map keyset id to a string with the keyset name */
const char *vcap_keyset_name(struct vcap_control *vctrl,
			     enum vcap_keyfield_set keyset)
{
	return vctrl->stats->keyfield_set_names[keyset];
}
EXPORT_SYMBOL_GPL(vcap_keyset_name);

/* map key field id to a string with the key name */
const char *vcap_keyfield_name(struct vcap_control *vctrl,
			       enum vcap_key_field key)
{
	return vctrl->stats->keyfield_names[key];
}
EXPORT_SYMBOL_GPL(vcap_keyfield_name);

/* map actionset id to a string with the actionset name */
const char *vcap_actionset_name(struct vcap_control *vctrl,
				enum vcap_actionfield_set actionset)
{
	return vctrl->stats->actionfield_set_names[actionset];
}

/* map action field id to a string with the action name */
const char *vcap_actionfield_name(struct vcap_control *vctrl,
				  enum vcap_action_field action)
{
	return vctrl->stats->actionfield_names[action];
}

/* Return the keyfield that matches a key in a keyset */
static const struct vcap_field *
vcap_find_keyset_keyfield(struct vcap_control *vctrl,
			  enum vcap_type vtype,
			  enum vcap_keyfield_set keyset,
			  enum vcap_key_field key)
{
	const struct vcap_field *fields;
	int idx, count;

	fields = vcap_keyfields(vctrl, vtype, keyset);
	if (!fields)
		return NULL;

	/* Iterate the keyfields of the keyset */
	count = vcap_keyfield_count(vctrl, vtype, keyset);
	for (idx = 0; idx < count; ++idx) {
		if (fields[idx].width == 0)
			continue;

		if (key == idx)
			return &fields[idx];
	}

	return NULL;
}

/* Match a list of keys against the keysets available in a vcap type */
static bool _vcap_rule_find_keysets(struct vcap_rule_internal *ri,
				    struct vcap_keyset_list *matches)
{
	const struct vcap_client_keyfield *ckf;
	int keyset, found, keycount, map_size;
	const struct vcap_field **map;
	enum vcap_type vtype;

	vtype = ri->admin->vtype;
	map = ri->vctrl->vcaps[vtype].keyfield_set_map;
	map_size = ri->vctrl->vcaps[vtype].keyfield_set_size;

	/* Get a count of the keyfields we want to match */
	keycount = 0;
	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list)
		++keycount;

	matches->cnt = 0;
	/* Iterate the keysets of the VCAP */
	for (keyset = 0; keyset < map_size; ++keyset) {
		if (!map[keyset])
			continue;

		/* Iterate the keys in the rule */
		found = 0;
		list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list)
			if (vcap_find_keyset_keyfield(ri->vctrl, vtype,
						      keyset, ckf->ctrl.key))
				++found;

		/* Save the keyset if all keyfields were found */
		if (found == keycount)
			if (!vcap_keyset_list_add(matches, keyset))
				/* bail out when the quota is filled */
				break;
	}

	return matches->cnt > 0;
}

/* Match a list of keys against the keysets available in a vcap type */
bool vcap_rule_find_keysets(struct vcap_rule *rule,
			    struct vcap_keyset_list *matches)
{
	struct vcap_rule_internal *ri = to_intrule(rule);

	return _vcap_rule_find_keysets(ri, matches);
}
EXPORT_SYMBOL_GPL(vcap_rule_find_keysets);

/* Validate a rule with respect to available port keys */
int vcap_val_rule(struct vcap_rule *rule, u16 l3_proto)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_keyset_list matches = {};
	enum vcap_keyfield_set keysets[10];
	int ret;

	ret = vcap_api_check(ri->vctrl);
	if (ret)
		return ret;
	if (!ri->admin) {
		ri->data.exterr = VCAP_ERR_NO_ADMIN;
		return -EINVAL;
	}
	if (!ri->ndev) {
		ri->data.exterr = VCAP_ERR_NO_NETDEV;
		return -EINVAL;
	}

	matches.keysets = keysets;
	matches.max = ARRAY_SIZE(keysets);
	if (ri->data.keyset == VCAP_KFS_NO_VALUE) {
		/* Iterate over rule keyfields and select keysets that fits */
		if (!_vcap_rule_find_keysets(ri, &matches)) {
			ri->data.exterr = VCAP_ERR_NO_KEYSET_MATCH;
			return -EINVAL;
		}
	} else {
		/* prepare for keyset validation */
		keysets[0] = ri->data.keyset;
		matches.cnt = 1;
	}

	/* Pick a keyset that is supported in the port lookups */
	ret = ri->vctrl->ops->validate_keyset(ri->ndev, ri->admin, rule,
					      &matches, l3_proto);
	if (ret < 0) {
		pr_err("%s:%d: keyset validation failed: %d\n",
		       __func__, __LINE__, ret);
		ri->data.exterr = VCAP_ERR_NO_PORT_KEYSET_MATCH;
		return ret;
	}
	/* use the keyset that is supported in the port lookups */
	ret = vcap_set_rule_set_keyset(rule, ret);
	if (ret < 0) {
		pr_err("%s:%d: keyset was not updated: %d\n",
		       __func__, __LINE__, ret);
		return ret;
	}
	if (ri->data.actionset == VCAP_AFS_NO_VALUE) {
		/* Later also actionsets will be matched against actions in
		 * the rule, and the type will be set accordingly
		 */
		ri->data.exterr = VCAP_ERR_NO_ACTIONSET_MATCH;
		return -EINVAL;
	}
	vcap_add_type_keyfield(rule);
	/* Add default fields to this rule */
	ri->vctrl->ops->add_default_fields(ri->ndev, ri->admin, rule);

	/* Rule size is the maximum of the entry and action subword count */
	ri->size = max(ri->keyset_sw, ri->actionset_sw);

	/* Finally check if there is room for the rule in the VCAP */
	return vcap_rule_space(ri->admin, ri->size);
}
EXPORT_SYMBOL_GPL(vcap_val_rule);

/* Entries are sorted with increasing values of sort_key.
 * I.e. Lowest numerical sort_key is first in list.
 * In order to locate largest keys first in list we negate the key size with
 * (max_size - size).
 */
static u32 vcap_sort_key(u32 max_size, u32 size, u8 user, u16 prio)
{
	return ((max_size - size) << 24) | (user << 16) | prio;
}

/* calculate the address of the next rule after this (lower address and prio) */
static u32 vcap_next_rule_addr(u32 addr, struct vcap_rule_internal *ri)
{
	return ((addr - ri->size) /  ri->size) * ri->size;
}

/* Assign a unique rule id and autogenerate one if id == 0 */
static u32 vcap_set_rule_id(struct vcap_rule_internal *ri)
{
	if (ri->data.id != 0)
		return ri->data.id;

	for (u32 next_id = 1; next_id < ~0; ++next_id) {
		if (!vcap_lookup_rule(ri->vctrl, next_id)) {
			ri->data.id = next_id;
			break;
		}
	}
	return ri->data.id;
}

static int vcap_insert_rule(struct vcap_rule_internal *ri,
			    struct vcap_rule_move *move)
{
	int sw_count = ri->vctrl->vcaps[ri->admin->vtype].sw_count;
	struct vcap_rule_internal *duprule, *iter, *elem = NULL;
	struct vcap_admin *admin = ri->admin;
	u32 addr;

	ri->sort_key = vcap_sort_key(sw_count, ri->size, ri->data.user,
				     ri->data.priority);

	/* Insert the new rule in the list of rule based on the sort key
	 * If the rule needs to be  inserted between existing rules then move
	 * these rules to make room for the new rule and update their start
	 * address.
	 */
	list_for_each_entry(iter, &admin->rules, list) {
		if (ri->sort_key < iter->sort_key) {
			elem = iter;
			break;
		}
	}

	if (!elem) {
		ri->addr = vcap_next_rule_addr(admin->last_used_addr, ri);
		admin->last_used_addr = ri->addr;

		/* Add a shallow copy of the rule to the VCAP list */
		duprule = vcap_dup_rule(ri);
		if (IS_ERR(duprule))
			return PTR_ERR(duprule);

		list_add_tail(&duprule->list, &admin->rules);
		return 0;
	}

	/* Reuse the space of the current rule */
	addr = elem->addr + elem->size;
	ri->addr = vcap_next_rule_addr(addr, ri);
	addr = ri->addr;

	/* Add a shallow copy of the rule to the VCAP list */
	duprule = vcap_dup_rule(ri);
	if (IS_ERR(duprule))
		return PTR_ERR(duprule);

	/* Add before the current entry */
	list_add_tail(&duprule->list, &elem->list);

	/* Update the current rule */
	elem->addr = vcap_next_rule_addr(addr, elem);
	addr = elem->addr;

	/* Update the address in the remaining rules in the list */
	list_for_each_entry_continue(elem, &admin->rules, list) {
		elem->addr = vcap_next_rule_addr(addr, elem);
		addr = elem->addr;
	}

	/* Update the move info */
	move->addr = admin->last_used_addr;
	move->count = ri->addr - addr;
	move->offset = admin->last_used_addr - addr;
	admin->last_used_addr = addr;
	return 0;
}

static void vcap_move_rules(struct vcap_rule_internal *ri,
			    struct vcap_rule_move *move)
{
	ri->vctrl->ops->move(ri->ndev, ri->admin, move->addr,
			 move->offset, move->count);
}

/* Encode and write a validated rule to the VCAP */
int vcap_add_rule(struct vcap_rule *rule)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_rule_move move = {0};
	int ret;

	ret = vcap_api_check(ri->vctrl);
	if (ret)
		return ret;
	/* Insert the new rule in the list of vcap rules */
	mutex_lock(&ri->admin->lock);
	ret = vcap_insert_rule(ri, &move);
	if (ret < 0) {
		pr_err("%s:%d: could not insert rule in vcap list: %d\n",
		       __func__, __LINE__, ret);
		goto out;
	}
	if (move.count > 0)
		vcap_move_rules(ri, &move);
	ret = vcap_encode_rule(ri);
	if (ret) {
		pr_err("%s:%d: rule encoding error: %d\n", __func__, __LINE__, ret);
		goto out;
	}

	ret = vcap_write_rule(ri);
	if (ret)
		pr_err("%s:%d: rule write error: %d\n", __func__, __LINE__, ret);
out:
	mutex_unlock(&ri->admin->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(vcap_add_rule);

/* Allocate a new rule with the provided arguments */
struct vcap_rule *vcap_alloc_rule(struct vcap_control *vctrl,
				  struct net_device *ndev, int vcap_chain_id,
				  enum vcap_user user, u16 priority,
				  u32 id)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;
	int err, maxsize;

	err = vcap_api_check(vctrl);
	if (err)
		return ERR_PTR(err);
	if (!ndev)
		return ERR_PTR(-ENODEV);
	/* Get the VCAP instance */
	admin = vcap_find_admin(vctrl, vcap_chain_id);
	if (!admin)
		return ERR_PTR(-ENOENT);
	/* Sanity check that this VCAP is supported on this platform */
	if (vctrl->vcaps[admin->vtype].rows == 0)
		return ERR_PTR(-EINVAL);
	/* Check if a rule with this id already exists */
	if (vcap_lookup_rule(vctrl, id))
		return ERR_PTR(-EEXIST);
	/* Check if there is room for the rule in the block(s) of the VCAP */
	maxsize = vctrl->vcaps[admin->vtype].sw_count; /* worst case rule size */
	if (vcap_rule_space(admin, maxsize))
		return ERR_PTR(-ENOSPC);
	/* Create a container for the rule and return it */
	ri = kzalloc(sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return ERR_PTR(-ENOMEM);
	ri->data.vcap_chain_id = vcap_chain_id;
	ri->data.user = user;
	ri->data.priority = priority;
	ri->data.id = id;
	ri->data.keyset = VCAP_KFS_NO_VALUE;
	ri->data.actionset = VCAP_AFS_NO_VALUE;
	INIT_LIST_HEAD(&ri->list);
	INIT_LIST_HEAD(&ri->data.keyfields);
	INIT_LIST_HEAD(&ri->data.actionfields);
	ri->ndev = ndev;
	ri->admin = admin; /* refer to the vcap instance */
	ri->vctrl = vctrl; /* refer to the client */
	if (vcap_set_rule_id(ri) == 0)
		goto out_free;
	vcap_erase_cache(ri);
	return (struct vcap_rule *)ri;

out_free:
	kfree(ri);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(vcap_alloc_rule);

/* Free mem of a rule owned by client after the rule as been added to the VCAP */
void vcap_free_rule(struct vcap_rule *rule)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_actionfield *caf, *next_caf;
	struct vcap_client_keyfield *ckf, *next_ckf;

	/* Deallocate the list of keys and actions */
	list_for_each_entry_safe(ckf, next_ckf, &ri->data.keyfields, ctrl.list) {
		list_del(&ckf->ctrl.list);
		kfree(ckf);
	}
	list_for_each_entry_safe(caf, next_caf, &ri->data.actionfields, ctrl.list) {
		list_del(&caf->ctrl.list);
		kfree(caf);
	}
	/* Deallocate the rule */
	kfree(rule);
}
EXPORT_SYMBOL_GPL(vcap_free_rule);

struct vcap_rule *vcap_get_rule(struct vcap_control *vctrl, u32 id)
{
	struct vcap_rule_internal *elem;
	struct vcap_rule_internal *ri;
	int err;

	ri = NULL;

	err = vcap_api_check(vctrl);
	if (err)
		return ERR_PTR(err);
	elem = vcap_lookup_rule(vctrl, id);
	if (!elem)
		return NULL;
	mutex_lock(&elem->admin->lock);
	ri = vcap_dup_rule(elem);
	if (IS_ERR(ri))
		goto unlock;
	err = vcap_read_rule(ri);
	if (err) {
		ri = ERR_PTR(err);
		goto unlock;
	}
	err = vcap_decode_keyset(ri);
	if (err) {
		ri = ERR_PTR(err);
		goto unlock;
	}
	err = vcap_decode_actionset(ri);
	if (err) {
		ri = ERR_PTR(err);
		goto unlock;
	}

unlock:
	mutex_unlock(&elem->admin->lock);
	return (struct vcap_rule *)ri;
}
EXPORT_SYMBOL_GPL(vcap_get_rule);

/* Update existing rule */
int vcap_mod_rule(struct vcap_rule *rule)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_counter ctr;
	int err;

	err = vcap_api_check(ri->vctrl);
	if (err)
		return err;

	if (!vcap_lookup_rule(ri->vctrl, ri->data.id))
		return -ENOENT;

	mutex_lock(&ri->admin->lock);
	/* Encode the bitstreams to the VCAP cache */
	vcap_erase_cache(ri);
	err = vcap_encode_rule(ri);
	if (err)
		goto out;

	err = vcap_write_rule(ri);
	if (err)
		goto out;

	memset(&ctr, 0, sizeof(ctr));
	err =  vcap_write_counter(ri, &ctr);
	if (err)
		goto out;

out:
	mutex_unlock(&ri->admin->lock);
	return err;
}
EXPORT_SYMBOL_GPL(vcap_mod_rule);

/* Return the alignment offset for a new rule address */
static int vcap_valid_rule_move(struct vcap_rule_internal *el, int offset)
{
	return (el->addr + offset) % el->size;
}

/* Update the rule address with an offset */
static void vcap_adjust_rule_addr(struct vcap_rule_internal *el, int offset)
{
	el->addr += offset;
}

/* Rules needs to be moved to fill the gap of the deleted rule */
static int vcap_fill_rule_gap(struct vcap_rule_internal *ri)
{
	struct vcap_admin *admin = ri->admin;
	struct vcap_rule_internal *elem;
	struct vcap_rule_move move;
	int gap = 0, offset = 0;

	/* If the first rule is deleted: Move other rules to the top */
	if (list_is_first(&ri->list, &admin->rules))
		offset = admin->last_valid_addr + 1 - ri->addr - ri->size;

	/* Locate gaps between odd size rules and adjust the move */
	elem = ri;
	list_for_each_entry_continue(elem, &admin->rules, list)
		gap += vcap_valid_rule_move(elem, ri->size);

	/* Update the address in the remaining rules in the list */
	elem = ri;
	list_for_each_entry_continue(elem, &admin->rules, list)
		vcap_adjust_rule_addr(elem, ri->size + gap + offset);

	/* Update the move info */
	move.addr = admin->last_used_addr;
	move.count = ri->addr - admin->last_used_addr - gap;
	move.offset = -(ri->size + gap + offset);

	/* Do the actual move operation */
	vcap_move_rules(ri, &move);

	return gap + offset;
}

/* Delete rule in a VCAP instance */
int vcap_del_rule(struct vcap_control *vctrl, struct net_device *ndev, u32 id)
{
	struct vcap_rule_internal *ri, *elem;
	struct vcap_admin *admin;
	int gap = 0, err;

	/* This will later also handle rule moving */
	if (!ndev)
		return -ENODEV;
	err = vcap_api_check(vctrl);
	if (err)
		return err;
	/* Look for the rule id in all vcaps */
	ri = vcap_lookup_rule(vctrl, id);
	if (!ri)
		return -EINVAL;
	admin = ri->admin;

	if (ri->addr > admin->last_used_addr)
		gap = vcap_fill_rule_gap(ri);

	/* Delete the rule from the list of rules and the cache */
	mutex_lock(&admin->lock);
	list_del(&ri->list);
	vctrl->ops->init(ndev, admin, admin->last_used_addr, ri->size + gap);
	kfree(ri);
	mutex_unlock(&admin->lock);

	/* Update the last used address, set to default when no rules */
	if (list_empty(&admin->rules)) {
		admin->last_used_addr = admin->last_valid_addr + 1;
	} else {
		elem = list_last_entry(&admin->rules, struct vcap_rule_internal,
				       list);
		admin->last_used_addr = elem->addr;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_del_rule);

/* Delete all rules in the VCAP instance */
int vcap_del_rules(struct vcap_control *vctrl, struct vcap_admin *admin)
{
	struct vcap_enabled_port *eport, *next_eport;
	struct vcap_rule_internal *ri, *next_ri;
	int ret = vcap_api_check(vctrl);

	if (ret)
		return ret;

	mutex_lock(&admin->lock);
	list_for_each_entry_safe(ri, next_ri, &admin->rules, list) {
		vctrl->ops->init(ri->ndev, admin, ri->addr, ri->size);
		list_del(&ri->list);
		kfree(ri);
	}
	admin->last_used_addr = admin->last_valid_addr;

	/* Remove list of enabled ports */
	list_for_each_entry_safe(eport, next_eport, &admin->enabled, list) {
		list_del(&eport->list);
		kfree(eport);
	}
	mutex_unlock(&admin->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(vcap_del_rules);

/* Find a client key field in a rule */
static struct vcap_client_keyfield *
vcap_find_keyfield(struct vcap_rule *rule, enum vcap_key_field key)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_keyfield *ckf;

	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list)
		if (ckf->ctrl.key == key)
			return ckf;
	return NULL;
}

/* Find information on a key field in a rule */
const struct vcap_field *vcap_lookup_keyfield(struct vcap_rule *rule,
					      enum vcap_key_field key)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	enum vcap_keyfield_set keyset = rule->keyset;
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_field *fields;

	if (keyset == VCAP_KFS_NO_VALUE)
		return NULL;
	fields = vcap_keyfields(ri->vctrl, vt, keyset);
	if (!fields)
		return NULL;
	return &fields[key];
}
EXPORT_SYMBOL_GPL(vcap_lookup_keyfield);

/* Copy data from src to dst but reverse the data in chunks of 32bits.
 * For example if src is 00:11:22:33:44:55 where 55 is LSB the dst will
 * have the value 22:33:44:55:00:11.
 */
static void vcap_copy_to_w32be(u8 *dst, u8 *src, int size)
{
	for (int idx = 0; idx < size; ++idx) {
		int first_byte_index = 0;
		int nidx;

		first_byte_index = size - (((idx >> 2) + 1) << 2);
		if (first_byte_index < 0)
			first_byte_index = 0;
		nidx = idx + first_byte_index - (idx & ~0x3);
		dst[nidx] = src[idx];
	}
}

static void vcap_copy_from_client_keyfield(struct vcap_rule *rule,
					   struct vcap_client_keyfield *field,
					   struct vcap_client_keyfield_data *data)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	int size;

	if (!ri->admin->w32be) {
		memcpy(&field->data, data, sizeof(field->data));
		return;
	}

	size = keyfield_size_table[field->ctrl.type] / 2;
	switch (field->ctrl.type) {
	case VCAP_FIELD_BIT:
	case VCAP_FIELD_U32:
		memcpy(&field->data, data, sizeof(field->data));
		break;
	case VCAP_FIELD_U48:
		vcap_copy_to_w32be(field->data.u48.value, data->u48.value, size);
		vcap_copy_to_w32be(field->data.u48.mask,  data->u48.mask, size);
		break;
	case VCAP_FIELD_U56:
		vcap_copy_to_w32be(field->data.u56.value, data->u56.value, size);
		vcap_copy_to_w32be(field->data.u56.mask,  data->u56.mask, size);
		break;
	case VCAP_FIELD_U64:
		vcap_copy_to_w32be(field->data.u64.value, data->u64.value, size);
		vcap_copy_to_w32be(field->data.u64.mask,  data->u64.mask, size);
		break;
	case VCAP_FIELD_U72:
		vcap_copy_to_w32be(field->data.u72.value, data->u72.value, size);
		vcap_copy_to_w32be(field->data.u72.mask,  data->u72.mask, size);
		break;
	case VCAP_FIELD_U112:
		vcap_copy_to_w32be(field->data.u112.value, data->u112.value, size);
		vcap_copy_to_w32be(field->data.u112.mask,  data->u112.mask, size);
		break;
	case VCAP_FIELD_U128:
		vcap_copy_to_w32be(field->data.u128.value, data->u128.value, size);
		vcap_copy_to_w32be(field->data.u128.mask,  data->u128.mask, size);
		break;
	}
}

/* Check if the keyfield is already in the rule */
static bool vcap_keyfield_unique(struct vcap_rule *rule,
				 enum vcap_key_field key)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	const struct vcap_client_keyfield *ckf;

	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list)
		if (ckf->ctrl.key == key)
			return false;
	return true;
}

/* Check if the keyfield is in the keyset */
static bool vcap_keyfield_match_keyset(struct vcap_rule *rule,
				       enum vcap_key_field key)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	enum vcap_keyfield_set keyset = rule->keyset;
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_field *fields;

	/* the field is accepted if the rule has no keyset yet */
	if (keyset == VCAP_KFS_NO_VALUE)
		return true;
	fields = vcap_keyfields(ri->vctrl, vt, keyset);
	if (!fields)
		return false;
	/* if there is a width there is a way */
	return fields[key].width > 0;
}

static int vcap_rule_add_key(struct vcap_rule *rule,
			     enum vcap_key_field key,
			     enum vcap_field_type ftype,
			     struct vcap_client_keyfield_data *data)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_keyfield *field;

	if (!vcap_keyfield_unique(rule, key)) {
		pr_warn("%s:%d: keyfield %s is already in the rule\n",
			__func__, __LINE__,
			vcap_keyfield_name(ri->vctrl, key));
		return -EINVAL;
	}

	if (!vcap_keyfield_match_keyset(rule, key)) {
		pr_err("%s:%d: keyfield %s does not belong in the rule keyset\n",
		       __func__, __LINE__,
		       vcap_keyfield_name(ri->vctrl, key));
		return -EINVAL;
	}

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return -ENOMEM;
	field->ctrl.key = key;
	field->ctrl.type = ftype;
	vcap_copy_from_client_keyfield(rule, field, data);
	list_add_tail(&field->ctrl.list, &rule->keyfields);
	return 0;
}

static void vcap_rule_set_key_bitsize(struct vcap_u1_key *u1, enum vcap_bit val)
{
	switch (val) {
	case VCAP_BIT_0:
		u1->value = 0;
		u1->mask = 1;
		break;
	case VCAP_BIT_1:
		u1->value = 1;
		u1->mask = 1;
		break;
	case VCAP_BIT_ANY:
		u1->value = 0;
		u1->mask = 0;
		break;
	}
}

/* Add a bit key with value and mask to the rule */
int vcap_rule_add_key_bit(struct vcap_rule *rule, enum vcap_key_field key,
			  enum vcap_bit val)
{
	struct vcap_client_keyfield_data data;

	vcap_rule_set_key_bitsize(&data.u1, val);
	return vcap_rule_add_key(rule, key, VCAP_FIELD_BIT, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_key_bit);

/* Add a 32 bit key field with value and mask to the rule */
int vcap_rule_add_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 value, u32 mask)
{
	struct vcap_client_keyfield_data data;

	data.u32.value = value;
	data.u32.mask = mask;
	return vcap_rule_add_key(rule, key, VCAP_FIELD_U32, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_key_u32);

/* Add a 48 bit key with value and mask to the rule */
int vcap_rule_add_key_u48(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u48_key *fieldval)
{
	struct vcap_client_keyfield_data data;

	memcpy(&data.u48, fieldval, sizeof(data.u48));
	return vcap_rule_add_key(rule, key, VCAP_FIELD_U48, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_key_u48);

/* Add a 72 bit key with value and mask to the rule */
int vcap_rule_add_key_u72(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u72_key *fieldval)
{
	struct vcap_client_keyfield_data data;

	memcpy(&data.u72, fieldval, sizeof(data.u72));
	return vcap_rule_add_key(rule, key, VCAP_FIELD_U72, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_key_u72);

/* Add a 128 bit key with value and mask to the rule */
int vcap_rule_add_key_u128(struct vcap_rule *rule, enum vcap_key_field key,
			   struct vcap_u128_key *fieldval)
{
	struct vcap_client_keyfield_data data;

	memcpy(&data.u128, fieldval, sizeof(data.u128));
	return vcap_rule_add_key(rule, key, VCAP_FIELD_U128, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_key_u128);

int vcap_rule_get_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 *value, u32 *mask)
{
	struct vcap_client_keyfield *ckf;

	ckf = vcap_find_keyfield(rule, key);
	if (!ckf)
		return -ENOENT;

	*value = ckf->data.u32.value;
	*mask = ckf->data.u32.mask;

	return 0;
}
EXPORT_SYMBOL_GPL(vcap_rule_get_key_u32);

/* Find a client action field in a rule */
static struct vcap_client_actionfield *
vcap_find_actionfield(struct vcap_rule *rule, enum vcap_action_field act)
{
	struct vcap_rule_internal *ri = (struct vcap_rule_internal *)rule;
	struct vcap_client_actionfield *caf;

	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list)
		if (caf->ctrl.action == act)
			return caf;
	return NULL;
}

static void vcap_copy_from_client_actionfield(struct vcap_rule *rule,
					      struct vcap_client_actionfield *field,
					      struct vcap_client_actionfield_data *data)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	int size;

	if (!ri->admin->w32be) {
		memcpy(&field->data, data, sizeof(field->data));
		return;
	}

	size = actionfield_size_table[field->ctrl.type];
	switch (field->ctrl.type) {
	case VCAP_FIELD_BIT:
	case VCAP_FIELD_U32:
		memcpy(&field->data, data, sizeof(field->data));
		break;
	case VCAP_FIELD_U48:
		vcap_copy_to_w32be(field->data.u48.value, data->u48.value, size);
		break;
	case VCAP_FIELD_U56:
		vcap_copy_to_w32be(field->data.u56.value, data->u56.value, size);
		break;
	case VCAP_FIELD_U64:
		vcap_copy_to_w32be(field->data.u64.value, data->u64.value, size);
		break;
	case VCAP_FIELD_U72:
		vcap_copy_to_w32be(field->data.u72.value, data->u72.value, size);
		break;
	case VCAP_FIELD_U112:
		vcap_copy_to_w32be(field->data.u112.value, data->u112.value, size);
		break;
	case VCAP_FIELD_U128:
		vcap_copy_to_w32be(field->data.u128.value, data->u128.value, size);
		break;
	}
}

/* Check if the actionfield is already in the rule */
static bool vcap_actionfield_unique(struct vcap_rule *rule,
				    enum vcap_action_field act)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	const struct vcap_client_actionfield *caf;

	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list)
		if (caf->ctrl.action == act)
			return false;
	return true;
}

/* Check if the actionfield is in the actionset */
static bool vcap_actionfield_match_actionset(struct vcap_rule *rule,
					     enum vcap_action_field action)
{
	enum vcap_actionfield_set actionset = rule->actionset;
	struct vcap_rule_internal *ri = to_intrule(rule);
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_field *fields;

	/* the field is accepted if the rule has no actionset yet */
	if (actionset == VCAP_AFS_NO_VALUE)
		return true;
	fields = vcap_actionfields(ri->vctrl, vt, actionset);
	if (!fields)
		return false;
	/* if there is a width there is a way */
	return fields[action].width > 0;
}

static int vcap_rule_add_action(struct vcap_rule *rule,
				enum vcap_action_field action,
				enum vcap_field_type ftype,
				struct vcap_client_actionfield_data *data)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_actionfield *field;

	if (!vcap_actionfield_unique(rule, action)) {
		pr_warn("%s:%d: actionfield %s is already in the rule\n",
			__func__, __LINE__,
			vcap_actionfield_name(ri->vctrl, action));
		return -EINVAL;
	}

	if (!vcap_actionfield_match_actionset(rule, action)) {
		pr_err("%s:%d: actionfield %s does not belong in the rule actionset\n",
		       __func__, __LINE__,
		       vcap_actionfield_name(ri->vctrl, action));
		return -EINVAL;
	}

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return -ENOMEM;
	field->ctrl.action = action;
	field->ctrl.type = ftype;
	vcap_copy_from_client_actionfield(rule, field, data);
	list_add_tail(&field->ctrl.list, &rule->actionfields);
	return 0;
}

static void vcap_rule_set_action_bitsize(struct vcap_u1_action *u1,
					 enum vcap_bit val)
{
	switch (val) {
	case VCAP_BIT_0:
		u1->value = 0;
		break;
	case VCAP_BIT_1:
		u1->value = 1;
		break;
	case VCAP_BIT_ANY:
		u1->value = 0;
		break;
	}
}

/* Add a bit action with value to the rule */
int vcap_rule_add_action_bit(struct vcap_rule *rule,
			     enum vcap_action_field action,
			     enum vcap_bit val)
{
	struct vcap_client_actionfield_data data;

	vcap_rule_set_action_bitsize(&data.u1, val);
	return vcap_rule_add_action(rule, action, VCAP_FIELD_BIT, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_action_bit);

/* Add a 32 bit action field with value to the rule */
int vcap_rule_add_action_u32(struct vcap_rule *rule,
			     enum vcap_action_field action,
			     u32 value)
{
	struct vcap_client_actionfield_data data;

	data.u32.value = value;
	return vcap_rule_add_action(rule, action, VCAP_FIELD_U32, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_action_u32);

static int vcap_read_counter(struct vcap_rule_internal *ri,
			     struct vcap_counter *ctr)
{
	struct vcap_admin *admin = ri->admin;

	ri->vctrl->ops->update(ri->ndev, admin, VCAP_CMD_READ, VCAP_SEL_COUNTER,
			       ri->addr);
	ri->vctrl->ops->cache_read(ri->ndev, admin, VCAP_SEL_COUNTER,
				   ri->counter_id, 0);
	ctr->value = admin->cache.counter;
	ctr->sticky = admin->cache.sticky;
	return 0;
}

/* Copy to host byte order */
void vcap_netbytes_copy(u8 *dst, u8 *src, int count)
{
	int idx;

	for (idx = 0; idx < count; ++idx, ++dst)
		*dst = src[count - idx - 1];
}
EXPORT_SYMBOL_GPL(vcap_netbytes_copy);

/* Convert validation error code into tc extact error message */
void vcap_set_tc_exterr(struct flow_cls_offload *fco, struct vcap_rule *vrule)
{
	switch (vrule->exterr) {
	case VCAP_ERR_NONE:
		break;
	case VCAP_ERR_NO_ADMIN:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Missing VCAP instance");
		break;
	case VCAP_ERR_NO_NETDEV:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Missing network interface");
		break;
	case VCAP_ERR_NO_KEYSET_MATCH:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No keyset matched the filter keys");
		break;
	case VCAP_ERR_NO_ACTIONSET_MATCH:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No actionset matched the filter actions");
		break;
	case VCAP_ERR_NO_PORT_KEYSET_MATCH:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No port keyset matched the filter keys");
		break;
	}
}
EXPORT_SYMBOL_GPL(vcap_set_tc_exterr);

/* Check if this port is already enabled for this VCAP instance */
static bool vcap_is_enabled(struct vcap_admin *admin, struct net_device *ndev,
			    unsigned long cookie)
{
	struct vcap_enabled_port *eport;

	list_for_each_entry(eport, &admin->enabled, list)
		if (eport->cookie == cookie || eport->ndev == ndev)
			return true;

	return false;
}

/* Enable this port for this VCAP instance */
static int vcap_enable(struct vcap_admin *admin, struct net_device *ndev,
		       unsigned long cookie)
{
	struct vcap_enabled_port *eport;

	eport = kzalloc(sizeof(*eport), GFP_KERNEL);
	if (!eport)
		return -ENOMEM;

	eport->ndev = ndev;
	eport->cookie = cookie;
	list_add_tail(&eport->list, &admin->enabled);

	return 0;
}

/* Disable this port for this VCAP instance */
static int vcap_disable(struct vcap_admin *admin, struct net_device *ndev,
			unsigned long cookie)
{
	struct vcap_enabled_port *eport;

	list_for_each_entry(eport, &admin->enabled, list) {
		if (eport->cookie == cookie && eport->ndev == ndev) {
			list_del(&eport->list);
			kfree(eport);
			return 0;
		}
	}

	return -ENOENT;
}

/* Find the VCAP instance that enabled the port using a specific filter */
static struct vcap_admin *vcap_find_admin_by_cookie(struct vcap_control *vctrl,
						    unsigned long cookie)
{
	struct vcap_enabled_port *eport;
	struct vcap_admin *admin;

	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(eport, &admin->enabled, list)
			if (eport->cookie == cookie)
				return admin;

	return NULL;
}

/* Enable/Disable the VCAP instance lookups. Chain id 0 means disable */
int vcap_enable_lookups(struct vcap_control *vctrl, struct net_device *ndev,
			int chain_id, unsigned long cookie, bool enable)
{
	struct vcap_admin *admin;
	int err;

	err = vcap_api_check(vctrl);
	if (err)
		return err;

	if (!ndev)
		return -ENODEV;

	if (chain_id)
		admin = vcap_find_admin(vctrl, chain_id);
	else
		admin = vcap_find_admin_by_cookie(vctrl, cookie);
	if (!admin)
		return -ENOENT;

	/* first instance and first chain */
	if (admin->vinst || chain_id > admin->first_cid)
		return -EFAULT;

	err = vctrl->ops->enable(ndev, admin, enable);
	if (err)
		return err;

	if (chain_id) {
		if (vcap_is_enabled(admin, ndev, cookie))
			return -EADDRINUSE;
		mutex_lock(&admin->lock);
		vcap_enable(admin, ndev, cookie);
	} else {
		mutex_lock(&admin->lock);
		vcap_disable(admin, ndev, cookie);
	}
	mutex_unlock(&admin->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(vcap_enable_lookups);

/* Set a rule counter id (for certain vcaps only) */
void vcap_rule_set_counter_id(struct vcap_rule *rule, u32 counter_id)
{
	struct vcap_rule_internal *ri = to_intrule(rule);

	ri->counter_id = counter_id;
}
EXPORT_SYMBOL_GPL(vcap_rule_set_counter_id);

/* Provide all rules via a callback interface */
int vcap_rule_iter(struct vcap_control *vctrl,
		   int (*callback)(void *, struct vcap_rule *), void *arg)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;
	int ret;

	ret = vcap_api_check(vctrl);
	if (ret)
		return ret;

	/* Iterate all rules in each VCAP instance */
	list_for_each_entry(admin, &vctrl->list, list) {
		list_for_each_entry(ri, &admin->rules, list) {
			ret = callback(arg, &ri->data);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vcap_rule_iter);

int vcap_rule_set_counter(struct vcap_rule *rule, struct vcap_counter *ctr)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	int err;

	err = vcap_api_check(ri->vctrl);
	if (err)
		return err;
	if (!ctr) {
		pr_err("%s:%d: counter is missing\n", __func__, __LINE__);
		return -EINVAL;
	}
	return vcap_write_counter(ri, ctr);
}
EXPORT_SYMBOL_GPL(vcap_rule_set_counter);

int vcap_rule_get_counter(struct vcap_rule *rule, struct vcap_counter *ctr)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	int err;

	err = vcap_api_check(ri->vctrl);
	if (err)
		return err;
	if (!ctr) {
		pr_err("%s:%d: counter is missing\n", __func__, __LINE__);
		return -EINVAL;
	}
	return vcap_read_counter(ri, ctr);
}
EXPORT_SYMBOL_GPL(vcap_rule_get_counter);

static int vcap_rule_mod_key(struct vcap_rule *rule,
			     enum vcap_key_field key,
			     enum vcap_field_type ftype,
			     struct vcap_client_keyfield_data *data)
{
	struct vcap_client_keyfield *field;

	field = vcap_find_keyfield(rule, key);
	if (!field)
		return vcap_rule_add_key(rule, key, ftype, data);
	vcap_copy_from_client_keyfield(rule, field, data);
	return 0;
}

/* Modify a 32 bit key field with value and mask in the rule */
int vcap_rule_mod_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 value, u32 mask)
{
	struct vcap_client_keyfield_data data;

	data.u32.value = value;
	data.u32.mask = mask;
	return vcap_rule_mod_key(rule, key, VCAP_FIELD_U32, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_mod_key_u32);

static int vcap_rule_mod_action(struct vcap_rule *rule,
				enum vcap_action_field action,
				enum vcap_field_type ftype,
				struct vcap_client_actionfield_data *data)
{
	struct vcap_client_actionfield *field;

	field = vcap_find_actionfield(rule, action);
	if (!field)
		return vcap_rule_add_action(rule, action, ftype, data);
	vcap_copy_from_client_actionfield(rule, field, data);
	return 0;
}

/* Modify a 32 bit action field with value in the rule */
int vcap_rule_mod_action_u32(struct vcap_rule *rule,
			     enum vcap_action_field action,
			     u32 value)
{
	struct vcap_client_actionfield_data data;

	data.u32.value = value;
	return vcap_rule_mod_action(rule, action, VCAP_FIELD_U32, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_mod_action_u32);

/* Drop keys in a keylist and any keys that are not supported by the keyset */
int vcap_filter_rule_keys(struct vcap_rule *rule,
			  enum vcap_key_field keylist[], int length,
			  bool drop_unsupported)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_keyfield *ckf, *next_ckf;
	const struct vcap_field *fields;
	enum vcap_key_field key;
	int err = 0;
	int idx;

	if (length > 0) {
		err = -EEXIST;
		list_for_each_entry_safe(ckf, next_ckf,
					 &ri->data.keyfields, ctrl.list) {
			key = ckf->ctrl.key;
			for (idx = 0; idx < length; ++idx)
				if (key == keylist[idx]) {
					list_del(&ckf->ctrl.list);
					kfree(ckf);
					idx++;
					err = 0;
				}
		}
	}
	if (drop_unsupported) {
		err = -EEXIST;
		fields = vcap_keyfields(ri->vctrl, ri->admin->vtype,
					rule->keyset);
		if (!fields)
			return err;
		list_for_each_entry_safe(ckf, next_ckf,
					 &ri->data.keyfields, ctrl.list) {
			key = ckf->ctrl.key;
			if (fields[key].width == 0) {
				list_del(&ckf->ctrl.list);
				kfree(ckf);
				err = 0;
			}
		}
	}
	return err;
}
EXPORT_SYMBOL_GPL(vcap_filter_rule_keys);

/* Make a full copy of an existing rule with a new rule id */
struct vcap_rule *vcap_copy_rule(struct vcap_rule *erule)
{
	struct vcap_rule_internal *ri = to_intrule(erule);
	struct vcap_client_actionfield *caf;
	struct vcap_client_keyfield *ckf;
	struct vcap_rule *rule;
	int err;

	err = vcap_api_check(ri->vctrl);
	if (err)
		return ERR_PTR(err);

	rule = vcap_alloc_rule(ri->vctrl, ri->ndev, ri->data.vcap_chain_id,
			       ri->data.user, ri->data.priority, 0);
	if (IS_ERR(rule))
		return rule;

	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list) {
		/* Add a key duplicate in the new rule */
		err = vcap_rule_add_key(rule,
					ckf->ctrl.key,
					ckf->ctrl.type,
					&ckf->data);
		if (err)
			goto err;
	}

	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list) {
		/* Add a action duplicate in the new rule */
		err = vcap_rule_add_action(rule,
					   caf->ctrl.action,
					   caf->ctrl.type,
					   &caf->data);
		if (err)
			goto err;
	}
	return rule;
err:
	vcap_free_rule(rule);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(vcap_copy_rule);

#ifdef CONFIG_VCAP_KUNIT_TEST
#include "vcap_api_kunit.c"
#endif
