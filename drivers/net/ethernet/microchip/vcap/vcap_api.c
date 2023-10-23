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

/* Stores the filter cookie and chain id that enabled the port */
struct vcap_enabled_port {
	struct list_head list; /* for insertion in enabled ports list */
	struct net_device *ndev;  /* the enabled port */
	unsigned long cookie; /* filter that enabled the port */
	int src_cid; /* source chain id */
	int dst_cid; /* destination chain id */
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

/* Copy data from src to dst but reverse the data in chunks of 32bits.
 * For example if src is 00:11:22:33:44:55 where 55 is LSB the dst will
 * have the value 22:33:44:55:00:11.
 */
static void vcap_copy_to_w32be(u8 *dst, const u8 *src, int size)
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

static void
vcap_copy_from_client_keyfield(struct vcap_rule *rule,
			       struct vcap_client_keyfield *dst,
			       const struct vcap_client_keyfield *src)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	const struct vcap_client_keyfield_data *sdata;
	struct vcap_client_keyfield_data *ddata;
	int size;

	dst->ctrl.type = src->ctrl.type;
	dst->ctrl.key = src->ctrl.key;
	INIT_LIST_HEAD(&dst->ctrl.list);
	sdata = &src->data;
	ddata = &dst->data;

	if (!ri->admin->w32be) {
		memcpy(ddata, sdata, sizeof(dst->data));
		return;
	}

	size = keyfield_size_table[dst->ctrl.type] / 2;

	switch (dst->ctrl.type) {
	case VCAP_FIELD_BIT:
	case VCAP_FIELD_U32:
		memcpy(ddata, sdata, sizeof(dst->data));
		break;
	case VCAP_FIELD_U48:
		vcap_copy_to_w32be(ddata->u48.value, src->data.u48.value, size);
		vcap_copy_to_w32be(ddata->u48.mask,  src->data.u48.mask, size);
		break;
	case VCAP_FIELD_U56:
		vcap_copy_to_w32be(ddata->u56.value, sdata->u56.value, size);
		vcap_copy_to_w32be(ddata->u56.mask,  sdata->u56.mask, size);
		break;
	case VCAP_FIELD_U64:
		vcap_copy_to_w32be(ddata->u64.value, sdata->u64.value, size);
		vcap_copy_to_w32be(ddata->u64.mask,  sdata->u64.mask, size);
		break;
	case VCAP_FIELD_U72:
		vcap_copy_to_w32be(ddata->u72.value, sdata->u72.value, size);
		vcap_copy_to_w32be(ddata->u72.mask,  sdata->u72.mask, size);
		break;
	case VCAP_FIELD_U112:
		vcap_copy_to_w32be(ddata->u112.value, sdata->u112.value, size);
		vcap_copy_to_w32be(ddata->u112.mask,  sdata->u112.mask, size);
		break;
	case VCAP_FIELD_U128:
		vcap_copy_to_w32be(ddata->u128.value, sdata->u128.value, size);
		vcap_copy_to_w32be(ddata->u128.mask,  sdata->u128.mask, size);
		break;
	}
}

static void
vcap_copy_from_client_actionfield(struct vcap_rule *rule,
				  struct vcap_client_actionfield *dst,
				  const struct vcap_client_actionfield *src)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	const struct vcap_client_actionfield_data *sdata;
	struct vcap_client_actionfield_data *ddata;
	int size;

	dst->ctrl.type = src->ctrl.type;
	dst->ctrl.action = src->ctrl.action;
	INIT_LIST_HEAD(&dst->ctrl.list);
	sdata = &src->data;
	ddata = &dst->data;

	if (!ri->admin->w32be) {
		memcpy(ddata, sdata, sizeof(dst->data));
		return;
	}

	size = actionfield_size_table[dst->ctrl.type];

	switch (dst->ctrl.type) {
	case VCAP_FIELD_BIT:
	case VCAP_FIELD_U32:
		memcpy(ddata, sdata, sizeof(dst->data));
		break;
	case VCAP_FIELD_U48:
		vcap_copy_to_w32be(ddata->u48.value, sdata->u48.value, size);
		break;
	case VCAP_FIELD_U56:
		vcap_copy_to_w32be(ddata->u56.value, sdata->u56.value, size);
		break;
	case VCAP_FIELD_U64:
		vcap_copy_to_w32be(ddata->u64.value, sdata->u64.value, size);
		break;
	case VCAP_FIELD_U72:
		vcap_copy_to_w32be(ddata->u72.value, sdata->u72.value, size);
		break;
	case VCAP_FIELD_U112:
		vcap_copy_to_w32be(ddata->u112.value, sdata->u112.value, size);
		break;
	case VCAP_FIELD_U128:
		vcap_copy_to_w32be(ddata->u128.value, sdata->u128.value, size);
		break;
	}
}

static int vcap_encode_rule_keyset(struct vcap_rule_internal *ri)
{
	const struct vcap_client_keyfield *ckf;
	const struct vcap_typegroup *tg_table;
	struct vcap_client_keyfield tempkf;
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
		vcap_copy_from_client_keyfield(&ri->data, &tempkf, ckf);
		vcap_encode_keyfield(ri, &tempkf, &kf_table[ckf->ctrl.key],
				     tg_table);
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
	struct vcap_client_actionfield tempaf;
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
		vcap_copy_from_client_actionfield(&ri->data, &tempaf, caf);
		vcap_encode_actionfield(ri, &tempaf,
					&af_table[caf->ctrl.action], tg_table);
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
	    !ctrl->ops->port_info) {
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

/* Check if a rule with this id exists */
static bool vcap_rule_exists(struct vcap_control *vctrl, u32 id)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(ri, &admin->rules, list)
			if (ri->data.id == id)
				return true;
	return false;
}

/* Find a rule with a provided rule id return a locked vcap */
static struct vcap_rule_internal *
vcap_get_locked_rule(struct vcap_control *vctrl, u32 id)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list) {
		mutex_lock(&admin->lock);
		list_for_each_entry(ri, &admin->rules, list)
			if (ri->data.id == id)
				return ri;
		mutex_unlock(&admin->lock);
	}
	return NULL;
}

/* Find a rule id with a provided cookie */
int vcap_lookup_rule_by_cookie(struct vcap_control *vctrl, u64 cookie)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;
	int id = 0;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list) {
		mutex_lock(&admin->lock);
		list_for_each_entry(ri, &admin->rules, list) {
			if (ri->data.cookie == cookie) {
				id = ri->data.id;
				break;
			}
		}
		mutex_unlock(&admin->lock);
		if (id)
			return id;
	}
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(vcap_lookup_rule_by_cookie);

/* Get number of rules in a vcap instance lookup chain id range */
int vcap_admin_rule_count(struct vcap_admin *admin, int cid)
{
	int max_cid = roundup(cid + 1, VCAP_CID_LOOKUP_SIZE);
	int min_cid = rounddown(cid, VCAP_CID_LOOKUP_SIZE);
	struct vcap_rule_internal *elem;
	int count = 0;

	list_for_each_entry(elem, &admin->rules, list) {
		mutex_lock(&admin->lock);
		if (elem->data.vcap_chain_id >= min_cid &&
		    elem->data.vcap_chain_id < max_cid)
			++count;
		mutex_unlock(&admin->lock);
	}
	return count;
}
EXPORT_SYMBOL_GPL(vcap_admin_rule_count);

/* Make a copy of the rule, shallow or full */
static struct vcap_rule_internal *vcap_dup_rule(struct vcap_rule_internal *ri,
						bool full)
{
	struct vcap_client_actionfield *caf, *newcaf;
	struct vcap_client_keyfield *ckf, *newckf;
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

	/* A full rule copy includes keys and actions */
	if (!full)
		return duprule;

	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list) {
		newckf = kmemdup(ckf, sizeof(*newckf), GFP_KERNEL);
		if (!newckf)
			goto err;
		list_add_tail(&newckf->ctrl.list, &duprule->data.keyfields);
	}

	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list) {
		newcaf = kmemdup(caf, sizeof(*newcaf), GFP_KERNEL);
		if (!newcaf)
			goto err;
		list_add_tail(&newcaf->ctrl.list, &duprule->data.actionfields);
	}

	return duprule;

err:
	list_for_each_entry_safe(ckf, newckf, &duprule->data.keyfields, ctrl.list) {
		list_del(&ckf->ctrl.list);
		kfree(ckf);
	}

	list_for_each_entry_safe(caf, newcaf, &duprule->data.actionfields, ctrl.list) {
		list_del(&caf->ctrl.list);
		kfree(caf);
	}

	kfree(duprule);
	return ERR_PTR(-ENOMEM);
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
		}
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
		}
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
		}
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
		}
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

/* Is this the last admin instance ordered by chain id and direction */
static bool vcap_admin_is_last(struct vcap_control *vctrl,
			       struct vcap_admin *admin,
			       bool ingress)
{
	struct vcap_admin *iter, *last = NULL;
	int max_cid = 0;

	list_for_each_entry(iter, &vctrl->list, list) {
		if (iter->first_cid > max_cid &&
		    iter->ingress == ingress) {
			last = iter;
			max_cid = iter->first_cid;
		}
	}
	if (!last)
		return false;

	return admin == last;
}

/* Calculate the value used for chaining VCAP rules */
int vcap_chain_offset(struct vcap_control *vctrl, int from_cid, int to_cid)
{
	int diff = to_cid - from_cid;

	if (diff < 0) /* Wrong direction */
		return diff;
	to_cid %= VCAP_CID_LOOKUP_SIZE;
	if (to_cid == 0)  /* Destination aligned to a lookup == no chaining */
		return 0;
	diff %= VCAP_CID_LOOKUP_SIZE;  /* Limit to a value within a lookup */
	return diff;
}
EXPORT_SYMBOL_GPL(vcap_chain_offset);

/* Is the next chain id in one of the following lookups
 * For now this does not support filters linked to other filters using
 * keys and actions. That will be added later.
 */
bool vcap_is_next_lookup(struct vcap_control *vctrl, int src_cid, int dst_cid)
{
	struct vcap_admin *admin;
	int next_cid;

	if (vcap_api_check(vctrl))
		return false;

	/* The offset must be at least one lookup so round up one chain */
	next_cid = roundup(src_cid + 1, VCAP_CID_LOOKUP_SIZE);

	if (dst_cid < next_cid)
		return false;

	admin = vcap_find_admin(vctrl, dst_cid);
	if (!admin)
		return false;

	return true;
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

/* Add the actionset typefield to the list of rule actionfields */
static int vcap_add_type_actionfield(struct vcap_rule *rule)
{
	enum vcap_actionfield_set actionset = rule->actionset;
	struct vcap_rule_internal *ri = to_intrule(rule);
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_field *fields;
	const struct vcap_set *aset;
	int ret = -EINVAL;

	aset = vcap_actionfieldset(ri->vctrl, vt, actionset);
	if (!aset)
		return ret;
	if (aset->type_id == (u8)-1)  /* No type field is needed */
		return 0;

	fields = vcap_actionfields(ri->vctrl, vt, actionset);
	if (!fields)
		return -EINVAL;
	if (fields[VCAP_AF_TYPE].width > 1) {
		ret = vcap_rule_add_action_u32(rule, VCAP_AF_TYPE,
					       aset->type_id);
	} else {
		if (aset->type_id)
			ret = vcap_rule_add_action_bit(rule, VCAP_AF_TYPE,
						       VCAP_BIT_1);
		else
			ret = vcap_rule_add_action_bit(rule, VCAP_AF_TYPE,
						       VCAP_BIT_0);
	}
	return ret;
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

/* Add a actionset to a actionset list */
static bool vcap_actionset_list_add(struct vcap_actionset_list *actionsetlist,
				    enum vcap_actionfield_set actionset)
{
	int idx;

	if (actionsetlist->cnt < actionsetlist->max) {
		/* Avoid duplicates */
		for (idx = 0; idx < actionsetlist->cnt; ++idx)
			if (actionsetlist->actionsets[idx] == actionset)
				return actionsetlist->cnt < actionsetlist->max;
		actionsetlist->actionsets[actionsetlist->cnt++] = actionset;
	}
	return actionsetlist->cnt < actionsetlist->max;
}

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

/* Return the actionfield that matches a action in a actionset */
static const struct vcap_field *
vcap_find_actionset_actionfield(struct vcap_control *vctrl,
				enum vcap_type vtype,
				enum vcap_actionfield_set actionset,
				enum vcap_action_field action)
{
	const struct vcap_field *fields;
	int idx, count;

	fields = vcap_actionfields(vctrl, vtype, actionset);
	if (!fields)
		return NULL;

	/* Iterate the actionfields of the actionset */
	count = vcap_actionfield_count(vctrl, vtype, actionset);
	for (idx = 0; idx < count; ++idx) {
		if (fields[idx].width == 0)
			continue;

		if (action == idx)
			return &fields[idx];
	}

	return NULL;
}

/* Match a list of actions against the actionsets available in a vcap type */
static bool vcap_rule_find_actionsets(struct vcap_rule_internal *ri,
				      struct vcap_actionset_list *matches)
{
	int actionset, found, actioncount, map_size;
	const struct vcap_client_actionfield *ckf;
	const struct vcap_field **map;
	enum vcap_type vtype;

	vtype = ri->admin->vtype;
	map = ri->vctrl->vcaps[vtype].actionfield_set_map;
	map_size = ri->vctrl->vcaps[vtype].actionfield_set_size;

	/* Get a count of the actionfields we want to match */
	actioncount = 0;
	list_for_each_entry(ckf, &ri->data.actionfields, ctrl.list)
		++actioncount;

	matches->cnt = 0;
	/* Iterate the actionsets of the VCAP */
	for (actionset = 0; actionset < map_size; ++actionset) {
		if (!map[actionset])
			continue;

		/* Iterate the actions in the rule */
		found = 0;
		list_for_each_entry(ckf, &ri->data.actionfields, ctrl.list)
			if (vcap_find_actionset_actionfield(ri->vctrl, vtype,
							    actionset,
							    ckf->ctrl.action))
				++found;

		/* Save the actionset if all actionfields were found */
		if (found == actioncount)
			if (!vcap_actionset_list_add(matches, actionset))
				/* bail out when the quota is filled */
				break;
	}

	return matches->cnt > 0;
}

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
		struct vcap_actionset_list matches = {};
		enum vcap_actionfield_set actionsets[10];

		matches.actionsets = actionsets;
		matches.max = ARRAY_SIZE(actionsets);

		/* Find an actionset that fits the rule actions */
		if (!vcap_rule_find_actionsets(ri, &matches)) {
			ri->data.exterr = VCAP_ERR_NO_ACTIONSET_MATCH;
			return -EINVAL;
		}
		ret = vcap_set_rule_set_actionset(rule, actionsets[0]);
		if (ret < 0) {
			pr_err("%s:%d: actionset was not updated: %d\n",
			       __func__, __LINE__, ret);
			return ret;
		}
	}
	vcap_add_type_keyfield(rule);
	vcap_add_type_actionfield(rule);
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
		if (!vcap_rule_exists(ri->vctrl, next_id)) {
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

		/* Add a copy of the rule to the VCAP list */
		duprule = vcap_dup_rule(ri, ri->state == VCAP_RS_DISABLED);
		if (IS_ERR(duprule))
			return PTR_ERR(duprule);

		list_add_tail(&duprule->list, &admin->rules);
		return 0;
	}

	/* Reuse the space of the current rule */
	addr = elem->addr + elem->size;
	ri->addr = vcap_next_rule_addr(addr, ri);
	addr = ri->addr;

	/* Add a copy of the rule to the VCAP list */
	duprule = vcap_dup_rule(ri, ri->state == VCAP_RS_DISABLED);
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

/* Check if the chain is already used to enable a VCAP lookup for this port */
static bool vcap_is_chain_used(struct vcap_control *vctrl,
			       struct net_device *ndev, int src_cid)
{
	struct vcap_enabled_port *eport;
	struct vcap_admin *admin;

	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(eport, &admin->enabled, list)
			if (eport->src_cid == src_cid && eport->ndev == ndev)
				return true;

	return false;
}

/* Fetch the next chain in the enabled list for the port */
static int vcap_get_next_chain(struct vcap_control *vctrl,
			       struct net_device *ndev,
			       int dst_cid)
{
	struct vcap_enabled_port *eport;
	struct vcap_admin *admin;

	list_for_each_entry(admin, &vctrl->list, list) {
		list_for_each_entry(eport, &admin->enabled, list) {
			if (eport->ndev != ndev)
				continue;
			if (eport->src_cid == dst_cid)
				return eport->dst_cid;
		}
	}

	return 0;
}

static bool vcap_path_exist(struct vcap_control *vctrl, struct net_device *ndev,
			    int dst_cid)
{
	int cid = rounddown(dst_cid, VCAP_CID_LOOKUP_SIZE);
	struct vcap_enabled_port *eport = NULL;
	struct vcap_enabled_port *elem;
	struct vcap_admin *admin;
	int tmp;

	if (cid == 0) /* Chain zero is always available */
		return true;

	/* Find first entry that starts from chain 0*/
	list_for_each_entry(admin, &vctrl->list, list) {
		list_for_each_entry(elem, &admin->enabled, list) {
			if (elem->src_cid == 0 && elem->ndev == ndev) {
				eport = elem;
				break;
			}
		}
		if (eport)
			break;
	}

	if (!eport)
		return false;

	tmp = eport->dst_cid;
	while (tmp != cid && tmp != 0)
		tmp = vcap_get_next_chain(vctrl, ndev, tmp);

	return !!tmp;
}

/* Internal clients can always store their rules in HW
 * External clients can store their rules if the chain is enabled all
 * the way from chain 0, otherwise the rule will be cached until
 * the chain is enabled.
 */
static void vcap_rule_set_state(struct vcap_rule_internal *ri)
{
	if (ri->data.user <= VCAP_USER_QOS)
		ri->state = VCAP_RS_PERMANENT;
	else if (vcap_path_exist(ri->vctrl, ri->ndev, ri->data.vcap_chain_id))
		ri->state = VCAP_RS_ENABLED;
	else
		ri->state = VCAP_RS_DISABLED;
}

/* Encode and write a validated rule to the VCAP */
int vcap_add_rule(struct vcap_rule *rule)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_rule_move move = {0};
	struct vcap_counter ctr = {0};
	int ret;

	ret = vcap_api_check(ri->vctrl);
	if (ret)
		return ret;
	/* Insert the new rule in the list of vcap rules */
	mutex_lock(&ri->admin->lock);

	vcap_rule_set_state(ri);
	ret = vcap_insert_rule(ri, &move);
	if (ret < 0) {
		pr_err("%s:%d: could not insert rule in vcap list: %d\n",
		       __func__, __LINE__, ret);
		goto out;
	}
	if (move.count > 0)
		vcap_move_rules(ri, &move);

	/* Set the counter to zero */
	ret = vcap_write_counter(ri, &ctr);
	if (ret)
		goto out;

	if (ri->state == VCAP_RS_DISABLED) {
		/* Erase the rule area */
		ri->vctrl->ops->init(ri->ndev, ri->admin, ri->addr, ri->size);
		goto out;
	}

	vcap_erase_cache(ri);
	ret = vcap_encode_rule(ri);
	if (ret) {
		pr_err("%s:%d: rule encoding error: %d\n", __func__, __LINE__, ret);
		goto out;
	}

	ret = vcap_write_rule(ri);
	if (ret) {
		pr_err("%s:%d: rule write error: %d\n", __func__, __LINE__, ret);
		goto out;
	}
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

	mutex_lock(&admin->lock);
	/* Check if a rule with this id already exists */
	if (vcap_rule_exists(vctrl, id)) {
		err = -EINVAL;
		goto out_unlock;
	}

	/* Check if there is room for the rule in the block(s) of the VCAP */
	maxsize = vctrl->vcaps[admin->vtype].sw_count; /* worst case rule size */
	if (vcap_rule_space(admin, maxsize)) {
		err = -ENOSPC;
		goto out_unlock;
	}

	/* Create a container for the rule and return it */
	ri = kzalloc(sizeof(*ri), GFP_KERNEL);
	if (!ri) {
		err = -ENOMEM;
		goto out_unlock;
	}

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

	if (vcap_set_rule_id(ri) == 0) {
		err = -EINVAL;
		goto out_free;
	}

	mutex_unlock(&admin->lock);
	return (struct vcap_rule *)ri;

out_free:
	kfree(ri);
out_unlock:
	mutex_unlock(&admin->lock);
	return ERR_PTR(err);

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

/* Decode a rule from the VCAP cache and return a copy */
struct vcap_rule *vcap_decode_rule(struct vcap_rule_internal *elem)
{
	struct vcap_rule_internal *ri;
	int err;

	ri = vcap_dup_rule(elem, elem->state == VCAP_RS_DISABLED);
	if (IS_ERR(ri))
		return ERR_CAST(ri);

	if (ri->state == VCAP_RS_DISABLED)
		goto out;

	err = vcap_read_rule(ri);
	if (err)
		return ERR_PTR(err);

	err = vcap_decode_keyset(ri);
	if (err)
		return ERR_PTR(err);

	err = vcap_decode_actionset(ri);
	if (err)
		return ERR_PTR(err);

out:
	return &ri->data;
}

struct vcap_rule *vcap_get_rule(struct vcap_control *vctrl, u32 id)
{
	struct vcap_rule_internal *elem;
	struct vcap_rule *rule;
	int err;

	err = vcap_api_check(vctrl);
	if (err)
		return ERR_PTR(err);

	elem = vcap_get_locked_rule(vctrl, id);
	if (!elem)
		return ERR_PTR(-ENOENT);

	rule = vcap_decode_rule(elem);
	mutex_unlock(&elem->admin->lock);
	return rule;
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

	if (!vcap_get_locked_rule(ri->vctrl, ri->data.id))
		return -ENOENT;

	vcap_rule_set_state(ri);
	if (ri->state == VCAP_RS_DISABLED)
		goto out;

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
	ri = vcap_get_locked_rule(vctrl, id);
	if (!ri)
		return -ENOENT;

	admin = ri->admin;

	if (ri->addr > admin->last_used_addr)
		gap = vcap_fill_rule_gap(ri);

	/* Delete the rule from the list of rules and the cache */
	list_del(&ri->list);
	vctrl->ops->init(ndev, admin, admin->last_used_addr, ri->size + gap);
	vcap_free_rule(&ri->data);

	/* Update the last used address, set to default when no rules */
	if (list_empty(&admin->rules)) {
		admin->last_used_addr = admin->last_valid_addr + 1;
	} else {
		elem = list_last_entry(&admin->rules, struct vcap_rule_internal,
				       list);
		admin->last_used_addr = elem->addr;
	}

	mutex_unlock(&admin->lock);
	return err;
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
		vcap_free_rule(&ri->data);
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
	memcpy(&field->data, data, sizeof(field->data));
	field->ctrl.key = key;
	field->ctrl.type = ftype;
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
struct vcap_client_actionfield *
vcap_find_actionfield(struct vcap_rule *rule, enum vcap_action_field act)
{
	struct vcap_rule_internal *ri = (struct vcap_rule_internal *)rule;
	struct vcap_client_actionfield *caf;

	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list)
		if (caf->ctrl.action == act)
			return caf;
	return NULL;
}
EXPORT_SYMBOL_GPL(vcap_find_actionfield);

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
	memcpy(&field->data, data, sizeof(field->data));
	field->ctrl.action = action;
	field->ctrl.type = ftype;
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

/* Write a rule to VCAP HW to enable it */
static int vcap_enable_rule(struct vcap_rule_internal *ri)
{
	struct vcap_client_actionfield *af, *naf;
	struct vcap_client_keyfield *kf, *nkf;
	int err;

	vcap_erase_cache(ri);
	err = vcap_encode_rule(ri);
	if (err)
		goto out;
	err = vcap_write_rule(ri);
	if (err)
		goto out;

	/* Deallocate the list of keys and actions */
	list_for_each_entry_safe(kf, nkf, &ri->data.keyfields, ctrl.list) {
		list_del(&kf->ctrl.list);
		kfree(kf);
	}
	list_for_each_entry_safe(af, naf, &ri->data.actionfields, ctrl.list) {
		list_del(&af->ctrl.list);
		kfree(af);
	}
	ri->state = VCAP_RS_ENABLED;
out:
	return err;
}

/* Enable all disabled rules for a specific chain/port in the VCAP HW */
static int vcap_enable_rules(struct vcap_control *vctrl,
			     struct net_device *ndev, int chain)
{
	int next_chain = chain + VCAP_CID_LOOKUP_SIZE;
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;
	int err = 0;

	list_for_each_entry(admin, &vctrl->list, list) {
		if (!(chain >= admin->first_cid && chain <= admin->last_cid))
			continue;

		/* Found the admin, now find the offloadable rules */
		mutex_lock(&admin->lock);
		list_for_each_entry(ri, &admin->rules, list) {
			/* Is the rule in the lookup defined by the chain */
			if (!(ri->data.vcap_chain_id >= chain &&
			      ri->data.vcap_chain_id < next_chain)) {
				continue;
			}

			if (ri->ndev != ndev)
				continue;

			if (ri->state != VCAP_RS_DISABLED)
				continue;

			err = vcap_enable_rule(ri);
			if (err)
				break;
		}
		mutex_unlock(&admin->lock);
		if (err)
			break;
	}
	return err;
}

/* Read and erase a rule from VCAP HW to disable it */
static int vcap_disable_rule(struct vcap_rule_internal *ri)
{
	int err;

	err = vcap_read_rule(ri);
	if (err)
		return err;
	err = vcap_decode_keyset(ri);
	if (err)
		return err;
	err = vcap_decode_actionset(ri);
	if (err)
		return err;

	ri->state = VCAP_RS_DISABLED;
	ri->vctrl->ops->init(ri->ndev, ri->admin, ri->addr, ri->size);
	return 0;
}

/* Disable all enabled rules for a specific chain/port in the VCAP HW */
static int vcap_disable_rules(struct vcap_control *vctrl,
			      struct net_device *ndev, int chain)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;
	int err = 0;

	list_for_each_entry(admin, &vctrl->list, list) {
		if (!(chain >= admin->first_cid && chain <= admin->last_cid))
			continue;

		/* Found the admin, now find the rules on the chain */
		mutex_lock(&admin->lock);
		list_for_each_entry(ri, &admin->rules, list) {
			if (ri->data.vcap_chain_id != chain)
				continue;

			if (ri->ndev != ndev)
				continue;

			if (ri->state != VCAP_RS_ENABLED)
				continue;

			err = vcap_disable_rule(ri);
			if (err)
				break;
		}
		mutex_unlock(&admin->lock);
		if (err)
			break;
	}
	return err;
}

/* Check if this port is already enabled for this VCAP instance */
static bool vcap_is_enabled(struct vcap_control *vctrl, struct net_device *ndev,
			    int dst_cid)
{
	struct vcap_enabled_port *eport;
	struct vcap_admin *admin;

	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(eport, &admin->enabled, list)
			if (eport->dst_cid == dst_cid && eport->ndev == ndev)
				return true;

	return false;
}

/* Enable this port and chain id in a VCAP instance */
static int vcap_enable(struct vcap_control *vctrl, struct net_device *ndev,
		       unsigned long cookie, int src_cid, int dst_cid)
{
	struct vcap_enabled_port *eport;
	struct vcap_admin *admin;

	if (src_cid >= dst_cid)
		return -EFAULT;

	admin = vcap_find_admin(vctrl, dst_cid);
	if (!admin)
		return -ENOENT;

	eport = kzalloc(sizeof(*eport), GFP_KERNEL);
	if (!eport)
		return -ENOMEM;

	eport->ndev = ndev;
	eport->cookie = cookie;
	eport->src_cid = src_cid;
	eport->dst_cid = dst_cid;
	mutex_lock(&admin->lock);
	list_add_tail(&eport->list, &admin->enabled);
	mutex_unlock(&admin->lock);

	if (vcap_path_exist(vctrl, ndev, src_cid)) {
		/* Enable chained lookups */
		while (dst_cid) {
			admin = vcap_find_admin(vctrl, dst_cid);
			if (!admin)
				return -ENOENT;

			vcap_enable_rules(vctrl, ndev, dst_cid);
			dst_cid = vcap_get_next_chain(vctrl, ndev, dst_cid);
		}
	}
	return 0;
}

/* Disable this port and chain id for a VCAP instance */
static int vcap_disable(struct vcap_control *vctrl, struct net_device *ndev,
			unsigned long cookie)
{
	struct vcap_enabled_port *elem, *eport = NULL;
	struct vcap_admin *found = NULL, *admin;
	int dst_cid;

	list_for_each_entry(admin, &vctrl->list, list) {
		list_for_each_entry(elem, &admin->enabled, list) {
			if (elem->cookie == cookie && elem->ndev == ndev) {
				eport = elem;
				found = admin;
				break;
			}
		}
		if (eport)
			break;
	}

	if (!eport)
		return -ENOENT;

	/* Disable chained lookups */
	dst_cid = eport->dst_cid;
	while (dst_cid) {
		admin = vcap_find_admin(vctrl, dst_cid);
		if (!admin)
			return -ENOENT;

		vcap_disable_rules(vctrl, ndev, dst_cid);
		dst_cid = vcap_get_next_chain(vctrl, ndev, dst_cid);
	}

	mutex_lock(&found->lock);
	list_del(&eport->list);
	mutex_unlock(&found->lock);
	kfree(eport);
	return 0;
}

/* Enable/Disable the VCAP instance lookups */
int vcap_enable_lookups(struct vcap_control *vctrl, struct net_device *ndev,
			int src_cid, int dst_cid, unsigned long cookie,
			bool enable)
{
	int err;

	err = vcap_api_check(vctrl);
	if (err)
		return err;

	if (!ndev)
		return -ENODEV;

	/* Source and destination must be the first chain in a lookup */
	if (src_cid % VCAP_CID_LOOKUP_SIZE)
		return -EFAULT;
	if (dst_cid % VCAP_CID_LOOKUP_SIZE)
		return -EFAULT;

	if (enable) {
		if (vcap_is_enabled(vctrl, ndev, dst_cid))
			return -EADDRINUSE;
		if (vcap_is_chain_used(vctrl, ndev, src_cid))
			return -EADDRNOTAVAIL;
		err = vcap_enable(vctrl, ndev, cookie, src_cid, dst_cid);
	} else {
		err = vcap_disable(vctrl, ndev, cookie);
	}

	return err;
}
EXPORT_SYMBOL_GPL(vcap_enable_lookups);

/* Is this chain id the last lookup of all VCAPs */
bool vcap_is_last_chain(struct vcap_control *vctrl, int cid, bool ingress)
{
	struct vcap_admin *admin;
	int lookup;

	if (vcap_api_check(vctrl))
		return false;

	admin = vcap_find_admin(vctrl, cid);
	if (!admin)
		return false;

	if (!vcap_admin_is_last(vctrl, admin, ingress))
		return false;

	/* This must be the last lookup in this VCAP type */
	lookup = vcap_chain_id_to_lookup(admin, cid);
	return lookup == admin->lookups - 1;
}
EXPORT_SYMBOL_GPL(vcap_is_last_chain);

/* Set a rule counter id (for certain vcaps only) */
void vcap_rule_set_counter_id(struct vcap_rule *rule, u32 counter_id)
{
	struct vcap_rule_internal *ri = to_intrule(rule);

	ri->counter_id = counter_id;
}
EXPORT_SYMBOL_GPL(vcap_rule_set_counter_id);

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

	mutex_lock(&ri->admin->lock);
	err = vcap_write_counter(ri, ctr);
	mutex_unlock(&ri->admin->lock);

	return err;
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

	mutex_lock(&ri->admin->lock);
	err = vcap_read_counter(ri, ctr);
	mutex_unlock(&ri->admin->lock);

	return err;
}
EXPORT_SYMBOL_GPL(vcap_rule_get_counter);

/* Get a copy of a client key field */
static int vcap_rule_get_key(struct vcap_rule *rule,
			     enum vcap_key_field key,
			     struct vcap_client_keyfield *ckf)
{
	struct vcap_client_keyfield *field;

	field = vcap_find_keyfield(rule, key);
	if (!field)
		return -EINVAL;
	memcpy(ckf, field, sizeof(*ckf));
	INIT_LIST_HEAD(&ckf->ctrl.list);
	return 0;
}

/* Find a keyset having the same size as the provided rule, where the keyset
 * does not have a type id.
 */
static int vcap_rule_get_untyped_keyset(struct vcap_rule_internal *ri,
					struct vcap_keyset_list *matches)
{
	struct vcap_control *vctrl = ri->vctrl;
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_set *keyfield_set;
	int idx;

	keyfield_set = vctrl->vcaps[vt].keyfield_set;
	for (idx = 0; idx < vctrl->vcaps[vt].keyfield_set_size; ++idx) {
		if (keyfield_set[idx].sw_per_item == ri->keyset_sw &&
		    keyfield_set[idx].type_id == (u8)-1) {
			vcap_keyset_list_add(matches, idx);
			return 0;
		}
	}
	return -EINVAL;
}

/* Get the keysets that matches the rule key type/mask */
int vcap_rule_get_keysets(struct vcap_rule_internal *ri,
			  struct vcap_keyset_list *matches)
{
	struct vcap_control *vctrl = ri->vctrl;
	enum vcap_type vt = ri->admin->vtype;
	const struct vcap_set *keyfield_set;
	struct vcap_client_keyfield kf = {};
	u32 value, mask;
	int err, idx;

	err = vcap_rule_get_key(&ri->data, VCAP_KF_TYPE, &kf);
	if (err)
		return vcap_rule_get_untyped_keyset(ri, matches);

	if (kf.ctrl.type == VCAP_FIELD_BIT) {
		value = kf.data.u1.value;
		mask = kf.data.u1.mask;
	} else if (kf.ctrl.type == VCAP_FIELD_U32) {
		value = kf.data.u32.value;
		mask = kf.data.u32.mask;
	} else {
		return -EINVAL;
	}

	keyfield_set = vctrl->vcaps[vt].keyfield_set;
	for (idx = 0; idx < vctrl->vcaps[vt].keyfield_set_size; ++idx) {
		if (keyfield_set[idx].sw_per_item != ri->keyset_sw)
			continue;

		if (keyfield_set[idx].type_id == (u8)-1) {
			vcap_keyset_list_add(matches, idx);
			continue;
		}

		if ((keyfield_set[idx].type_id & mask) == value)
			vcap_keyset_list_add(matches, idx);
	}
	if (matches->cnt > 0)
		return 0;

	return -EINVAL;
}

/* Collect packet counts from all rules with the same cookie */
int vcap_get_rule_count_by_cookie(struct vcap_control *vctrl,
				  struct vcap_counter *ctr, u64 cookie)
{
	struct vcap_rule_internal *ri;
	struct vcap_counter temp = {};
	struct vcap_admin *admin;
	int err;

	err = vcap_api_check(vctrl);
	if (err)
		return err;

	/* Iterate all rules in each VCAP instance */
	list_for_each_entry(admin, &vctrl->list, list) {
		mutex_lock(&admin->lock);
		list_for_each_entry(ri, &admin->rules, list) {
			if (ri->data.cookie != cookie)
				continue;

			err = vcap_read_counter(ri, &temp);
			if (err)
				goto unlock;
			ctr->value += temp.value;

			/* Reset the rule counter */
			temp.value = 0;
			temp.sticky = 0;
			err = vcap_write_counter(ri, &temp);
			if (err)
				goto unlock;
		}
		mutex_unlock(&admin->lock);
	}
	return err;

unlock:
	mutex_unlock(&admin->lock);
	return err;
}
EXPORT_SYMBOL_GPL(vcap_get_rule_count_by_cookie);

static int vcap_rule_mod_key(struct vcap_rule *rule,
			     enum vcap_key_field key,
			     enum vcap_field_type ftype,
			     struct vcap_client_keyfield_data *data)
{
	struct vcap_client_keyfield *field;

	field = vcap_find_keyfield(rule, key);
	if (!field)
		return vcap_rule_add_key(rule, key, ftype, data);
	memcpy(&field->data, data, sizeof(field->data));
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

/* Remove a key field with value and mask in the rule */
int vcap_rule_rem_key(struct vcap_rule *rule, enum vcap_key_field key)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_keyfield *field;

	field = vcap_find_keyfield(rule, key);
	if (!field) {
		pr_err("%s:%d: key %s is not in the rule\n",
		       __func__, __LINE__, vcap_keyfield_name(ri->vctrl, key));
		return -EINVAL;
	}
	/* Deallocate the key field */
	list_del(&field->ctrl.list);
	kfree(field);
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_rule_rem_key);

static int vcap_rule_mod_action(struct vcap_rule *rule,
				enum vcap_action_field action,
				enum vcap_field_type ftype,
				struct vcap_client_actionfield_data *data)
{
	struct vcap_client_actionfield *field;

	field = vcap_find_actionfield(rule, action);
	if (!field)
		return vcap_rule_add_action(rule, action, ftype, data);
	memcpy(&field->data, data, sizeof(field->data));
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

/* Select the keyset from the list that results in the smallest rule size */
enum vcap_keyfield_set
vcap_select_min_rule_keyset(struct vcap_control *vctrl,
			    enum vcap_type vtype,
			    struct vcap_keyset_list *kslist)
{
	enum vcap_keyfield_set ret = VCAP_KFS_NO_VALUE;
	const struct vcap_set *kset;
	int max = 100, idx;

	for (idx = 0; idx < kslist->cnt; ++idx) {
		kset = vcap_keyfieldset(vctrl, vtype, kslist->keysets[idx]);
		if (!kset)
			continue;
		if (kset->sw_per_item >= max)
			continue;
		max = kset->sw_per_item;
		ret = kslist->keysets[idx];
	}
	return ret;
}
EXPORT_SYMBOL_GPL(vcap_select_min_rule_keyset);

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
