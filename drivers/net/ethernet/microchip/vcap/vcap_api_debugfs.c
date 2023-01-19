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

/* Dump the keyfields value and mask values */
static void vcap_debugfs_show_rule_keyfield(struct vcap_control *vctrl,
					    struct vcap_output_print *out,
					    enum vcap_key_field key,
					    const struct vcap_field *keyfield,
					    struct vcap_client_keyfield_data *data)
{
	bool hex = false;
	u8 *value, *mask;
	int idx, bytes;

	out->prf(out->dst, "    %s: W%d: ", vcap_keyfield_name(vctrl, key),
		 keyfield[key].width);

	switch (keyfield[key].type) {
	case VCAP_FIELD_BIT:
		out->prf(out->dst, "%d/%d", data->u1.value, data->u1.mask);
		break;
	case VCAP_FIELD_U32:
		value = (u8 *)(&data->u32.value);
		mask = (u8 *)(&data->u32.mask);

		if (key == VCAP_KF_L3_IP4_SIP || key == VCAP_KF_L3_IP4_DIP) {
			out->prf(out->dst, "%pI4h/%pI4h", &data->u32.value,
				 &data->u32.mask);
		} else if (key == VCAP_KF_ETYPE ||
			   key == VCAP_KF_IF_IGR_PORT_MASK) {
			hex = true;
		} else {
			u32 fmsk = (1 << keyfield[key].width) - 1;

			out->prf(out->dst, "%u/%u", data->u32.value & fmsk,
				 data->u32.mask & fmsk);
		}
		break;
	case VCAP_FIELD_U48:
		value = data->u48.value;
		mask = data->u48.mask;
		if (key == VCAP_KF_L2_SMAC || key == VCAP_KF_L2_DMAC)
			out->prf(out->dst, "%pMR/%pMR", data->u48.value,
				 data->u48.mask);
		else
			hex = true;
		break;
	case VCAP_FIELD_U56:
		value = data->u56.value;
		mask = data->u56.mask;
		hex = true;
		break;
	case VCAP_FIELD_U64:
		value = data->u64.value;
		mask = data->u64.mask;
		hex = true;
		break;
	case VCAP_FIELD_U72:
		value = data->u72.value;
		mask = data->u72.mask;
		hex = true;
		break;
	case VCAP_FIELD_U112:
		value = data->u112.value;
		mask = data->u112.mask;
		hex = true;
		break;
	case VCAP_FIELD_U128:
		value = data->u128.value;
		mask = data->u128.mask;
		if (key == VCAP_KF_L3_IP6_SIP || key == VCAP_KF_L3_IP6_DIP) {
			u8 nvalue[16], nmask[16];

			vcap_netbytes_copy(nvalue, data->u128.value,
					   sizeof(nvalue));
			vcap_netbytes_copy(nmask, data->u128.mask,
					   sizeof(nmask));
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
	struct vcap_admin *admin = ri->admin;
	enum vcap_keyfield_set keysets[10];
	const struct vcap_field *keyfield;
	enum vcap_type vt = admin->vtype;
	struct vcap_client_keyfield *ckf;
	struct vcap_keyset_list matches;
	u32 *maskstream;
	u32 *keystream;
	int res;

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
	out->prf(out->dst, "  keysets:");
	for (int idx = 0; idx < matches.cnt; ++idx)
		out->prf(out->dst, " %s",
			 vcap_keyset_name(vctrl, matches.keysets[idx]));
	out->prf(out->dst, "\n");
	out->prf(out->dst, "  keyset_sw: %d\n", ri->keyset_sw);
	out->prf(out->dst, "  keyset_sw_regs: %d\n", ri->keyset_sw_regs);

	list_for_each_entry(ckf, &ri->data.keyfields, ctrl.list) {
		keyfield = vcap_keyfields(vctrl, admin->vtype, ri->data.keyset);
		vcap_debugfs_show_rule_keyfield(vctrl, out, ckf->ctrl.key,
						keyfield, &ckf->data);
	}

	return 0;
}

static int vcap_debugfs_show_rule_actionset(struct vcap_rule_internal *ri,
					    struct vcap_output_print *out)
{
	struct vcap_control *vctrl = ri->vctrl;
	struct vcap_admin *admin = ri->admin;
	const struct vcap_field *actionfield;
	struct vcap_client_actionfield *caf;

	out->prf(out->dst, "  actionset: %s\n",
		 vcap_actionset_name(vctrl, ri->data.actionset));
	out->prf(out->dst, "  actionset_sw: %d\n", ri->actionset_sw);
	out->prf(out->dst, "  actionset_sw_regs: %d\n", ri->actionset_sw_regs);

	list_for_each_entry(caf, &ri->data.actionfields, ctrl.list) {
		actionfield = vcap_actionfields(vctrl, admin->vtype,
						ri->data.actionset);
		vcap_debugfs_show_rule_actionfield(vctrl, out, caf->ctrl.action,
						   actionfield,
						   &caf->data.u1.value);
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
	struct vcap_rule_internal *elem;
	struct vcap_rule *vrule;
	int ret = 0;

	vcap_show_admin_info(vctrl, admin, out);
	list_for_each_entry(elem, &admin->rules, list) {
		vrule = vcap_get_rule(vctrl, elem->data.id);
		if (IS_ERR_OR_NULL(vrule)) {
			ret = PTR_ERR(vrule);
			break;
		}

		out->prf(out->dst, "\n");
		vcap_show_admin_rule(vctrl, admin, out, to_intrule(vrule));
		vcap_free_rule(vrule);
	}
	return ret;
}

static int vcap_show_admin_raw(struct vcap_control *vctrl,
			       struct vcap_admin *admin,
			       struct vcap_output_print *out)
{
	enum vcap_keyfield_set keysets[10];
	enum vcap_type vt = admin->vtype;
	struct vcap_keyset_list kslist;
	struct vcap_rule_internal *ri;
	const struct vcap_set *info;
	int addr, idx;
	int ret;

	if (list_empty(&admin->rules))
		return 0;

	ret = vcap_api_check(vctrl);
	if (ret)
		return ret;

	ri = list_first_entry(&admin->rules, struct vcap_rule_internal, list);

	/* Go from higher to lower addresses searching for a keyset */
	kslist.keysets = keysets;
	kslist.max = ARRAY_SIZE(keysets);
	for (addr = admin->last_valid_addr; addr >= admin->first_valid_addr;
	     --addr) {
		kslist.cnt = 0;
		ret = vcap_addr_keysets(vctrl, ri->ndev, admin, addr, &kslist);
		if (ret < 0)
			continue;
		info = vcap_keyfieldset(vctrl, vt, kslist.keysets[0]);
		if (!info)
			continue;
		if (addr % info->sw_per_item) {
			pr_info("addr: %d X%d error rule, keyset: %s\n",
				addr,
				info->sw_per_item,
				vcap_keyset_name(vctrl, kslist.keysets[0]));
		} else {
			out->prf(out->dst, "  addr: %d, X%d rule, keysets:",
				 addr,
				 info->sw_per_item);
			for (idx = 0; idx < kslist.cnt; ++idx)
				out->prf(out->dst, " %s",
					 vcap_keyset_name(vctrl,
							  kslist.keysets[idx]));
			out->prf(out->dst, "\n");
		}
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
