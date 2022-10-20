// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/types.h>

#include "vcap_api.h"
#include "vcap_api_client.h"

#define to_intrule(rule) container_of((rule), struct vcap_rule_internal, data)

/* Private VCAP API rule data */
struct vcap_rule_internal {
	struct vcap_rule data; /* provided by the client */
	struct list_head list; /* for insertion in the vcap admin list of rules */
	struct vcap_admin *admin; /* vcap hw instance */
	struct net_device *ndev;  /* the interface that the rule applies to */
	struct vcap_control *vctrl; /* the client control */
	u32 sort_key;  /* defines the position in the VCAP */
	int keyset_sw;  /* subwords in a keyset */
	int actionset_sw;  /* subwords in an actionset */
	int keyset_sw_regs;  /* registers in a subword in an keyset */
	int actionset_sw_regs;  /* registers in a subword in an actionset */
	int size; /* the size of the rule: max(entry, action) */
	u32 addr; /* address in the VCAP at insertion */
};

/* Moving a rule in the VCAP address space */
struct vcap_rule_move {
	int addr; /* address to move */
	int offset; /* change in address */
	int count; /* blocksize of addresses to move */
};

/* Bit iterator for the VCAP cache streams */
struct vcap_stream_iter {
	u32 offset; /* bit offset from the stream start */
	u32 sw_width; /* subword width in bits */
	u32 regs_per_sw; /* registers per subword */
	u32 reg_idx; /* current register index */
	u32 reg_bitpos; /* bit offset in current register */
	const struct vcap_typegroup *tg; /* current typegroup */
};

static void vcap_iter_set(struct vcap_stream_iter *itr, int sw_width,
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

static void vcap_iter_update(struct vcap_stream_iter *itr)
{
	int sw_idx, sw_bitpos;

	/* Calculate the subword index and bitposition for current bit */
	sw_idx = itr->offset / itr->sw_width;
	sw_bitpos = itr->offset % itr->sw_width;
	/* Calculate the register index and bitposition for current bit */
	itr->reg_idx = (sw_idx * itr->regs_per_sw) + (sw_bitpos / 32);
	itr->reg_bitpos = sw_bitpos % 32;
}

static void vcap_iter_init(struct vcap_stream_iter *itr, int sw_width,
			   const struct vcap_typegroup *tg, u32 offset)
{
	vcap_iter_set(itr, sw_width, tg, offset);
	vcap_iter_skip_tg(itr);
	vcap_iter_update(itr);
}

static void vcap_iter_next(struct vcap_stream_iter *itr)
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

/* Return the list of keyfields for the keyset */
static const struct vcap_field *vcap_keyfields(struct vcap_control *vctrl,
					       enum vcap_type vt,
					       enum vcap_keyfield_set keyset)
{
	/* Check that the keyset exists in the vcap keyset list */
	if (keyset >= vctrl->vcaps[vt].keyfield_set_size)
		return NULL;
	return vctrl->vcaps[vt].keyfield_set_map[keyset];
}

/* Return the keyset information for the keyset */
static const struct vcap_set *vcap_keyfieldset(struct vcap_control *vctrl,
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

/* Return the typegroup table for the matching keyset (using subword size) */
static const struct vcap_typegroup *
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
static int vcap_keyfield_count(struct vcap_control *vctrl,
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
static const struct vcap_field *
vcap_actionfields(struct vcap_control *vctrl,
		  enum vcap_type vt, enum vcap_actionfield_set actionset)
{
	/* Check that the actionset exists in the vcap actionset list */
	if (actionset >= vctrl->vcaps[vt].actionfield_set_size)
		return NULL;
	return vctrl->vcaps[vt].actionfield_set_map[actionset];
}

static const struct vcap_set *
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
static const struct vcap_typegroup *
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
static int vcap_actionfield_count(struct vcap_control *vctrl,
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

static int vcap_api_check(struct vcap_control *ctrl)
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

static void vcap_erase_cache(struct vcap_rule_internal *ri)
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
static struct vcap_rule_internal *vcap_dup_rule(struct vcap_rule_internal *ri)
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

/* Validate a rule with respect to available port keys */
int vcap_val_rule(struct vcap_rule *rule, u16 l3_proto)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	enum vcap_keyfield_set keysets[10];
	struct vcap_keyset_list kslist;
	int ret;

	/* This validation will be much expanded later */
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
	if (ri->data.keyset == VCAP_KFS_NO_VALUE) {
		ri->data.exterr = VCAP_ERR_NO_KEYSET_MATCH;
		return -EINVAL;
	}
	/* prepare for keyset validation */
	keysets[0] = ri->data.keyset;
	kslist.keysets = keysets;
	kslist.cnt = 1;
	/* Pick a keyset that is supported in the port lookups */
	ret = ri->vctrl->ops->validate_keyset(ri->ndev, ri->admin, rule, &kslist,
					      l3_proto);
	if (ret < 0) {
		pr_err("%s:%d: keyset validation failed: %d\n",
		       __func__, __LINE__, ret);
		ri->data.exterr = VCAP_ERR_NO_PORT_KEYSET_MATCH;
		return ret;
	}
	if (ri->data.actionset == VCAP_AFS_NO_VALUE) {
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

/* calculate the address of the next rule after this (lower address and prio) */
static u32 vcap_next_rule_addr(u32 addr, struct vcap_rule_internal *ri)
{
	return ((addr - ri->size) /  ri->size) * ri->size;
}

/* Assign a unique rule id and autogenerate one if id == 0 */
static u32 vcap_set_rule_id(struct vcap_rule_internal *ri)
{
	u32 next_id;

	if (ri->data.id != 0)
		return ri->data.id;

	next_id = ri->vctrl->rule_id + 1;

	for (next_id = ri->vctrl->rule_id + 1; next_id < ~0; ++next_id) {
		if (!vcap_lookup_rule(ri->vctrl, next_id)) {
			ri->data.id = next_id;
			ri->vctrl->rule_id = next_id;
			break;
		}
	}
	return ri->data.id;
}

static int vcap_insert_rule(struct vcap_rule_internal *ri,
			    struct vcap_rule_move *move)
{
	struct vcap_admin *admin = ri->admin;
	struct vcap_rule_internal *duprule;

	/* Only support appending rules for now */
	ri->addr = vcap_next_rule_addr(admin->last_used_addr, ri);
	admin->last_used_addr = ri->addr;
	/* Add a shallow copy of the rule to the VCAP list */
	duprule = vcap_dup_rule(ri);
	if (IS_ERR(duprule))
		return PTR_ERR(duprule);
	list_add_tail(&duprule->list, &admin->rules);
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
	int maxsize;

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

/* Delete rule in a VCAP instance */
int vcap_del_rule(struct vcap_control *vctrl, struct net_device *ndev, u32 id)
{
	struct vcap_rule_internal *ri, *elem;
	struct vcap_admin *admin;
	int err;

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
	list_del(&ri->list);

	/* delete the rule in the cache */
	vctrl->ops->init(ndev, admin, ri->addr, ri->size);
	if (list_empty(&admin->rules)) {
		admin->last_used_addr = admin->last_valid_addr;
	} else {
		/* update the address range end marker from the last rule in the list */
		elem = list_last_entry(&admin->rules, struct vcap_rule_internal, list);
		admin->last_used_addr = elem->addr;
	}
	kfree(ri);
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_del_rule);

/* Delete all rules in the VCAP instance */
int vcap_del_rules(struct vcap_control *vctrl, struct vcap_admin *admin)
{
	struct vcap_rule_internal *ri, *next_ri;
	int ret = vcap_api_check(vctrl);

	if (ret)
		return ret;
	list_for_each_entry_safe(ri, next_ri, &admin->rules, list) {
		vctrl->ops->init(ri->ndev, admin, ri->addr, ri->size);
		list_del(&ri->list);
		kfree(ri);
	}
	admin->last_used_addr = admin->last_valid_addr;
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_del_rules);

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

static void vcap_copy_from_client_keyfield(struct vcap_rule *rule,
					   struct vcap_client_keyfield *field,
					   struct vcap_client_keyfield_data *data)
{
	/* This will be expanded later to handle different vcap memory layouts */
	memcpy(&field->data, data, sizeof(field->data));
}

static int vcap_rule_add_key(struct vcap_rule *rule,
			     enum vcap_key_field key,
			     enum vcap_field_type ftype,
			     struct vcap_client_keyfield_data *data)
{
	struct vcap_client_keyfield *field;

	/* More validation will be added here later */
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

static void vcap_copy_from_client_actionfield(struct vcap_rule *rule,
					      struct vcap_client_actionfield *field,
					      struct vcap_client_actionfield_data *data)
{
	/* This will be expanded later to handle different vcap memory layouts */
	memcpy(&field->data, data, sizeof(field->data));
}

static int vcap_rule_add_action(struct vcap_rule *rule,
				enum vcap_action_field action,
				enum vcap_field_type ftype,
				struct vcap_client_actionfield_data *data)
{
	struct vcap_client_actionfield *field;

	/* More validation will be added here later */
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

#ifdef CONFIG_VCAP_KUNIT_TEST
#include "vcap_api_kunit.c"
#endif
