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
	}
	return dir;
}
EXPORT_SYMBOL_GPL(vcap_debugfs);
