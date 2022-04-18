// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Platform energy and frequency attributes driver
 *
 * This driver creates a sys file at /sys/firmware/papr/ which encapsulates a
 * directory structure containing files in keyword - value pairs that specify
 * energy and frequency configuration of the system.
 *
 * The format of exposing the sysfs information is as follows:
 * /sys/firmware/papr/energy_scale_info/
 *  |-- <id>/
 *    |-- desc
 *    |-- value
 *    |-- value_desc (if exists)
 *  |-- <id>/
 *    |-- desc
 *    |-- value
 *    |-- value_desc (if exists)
 *
 * Copyright 2022 IBM Corp.
 */

#include <asm/hvcall.h>
#include <asm/machdep.h>

#include "pseries.h"

/*
 * Flag attributes to fetch either all or one attribute from the HCALL
 * flag = BE(0) => fetch all attributes with firstAttributeId = 0
 * flag = BE(1) => fetch a single attribute with firstAttributeId = id
 */
#define ESI_FLAGS_ALL		0
#define ESI_FLAGS_SINGLE	(1ull << 63)

#define KOBJ_MAX_ATTRS		3

#define ESI_HDR_SIZE		sizeof(struct h_energy_scale_info_hdr)
#define ESI_ATTR_SIZE		sizeof(struct energy_scale_attribute)
#define CURR_MAX_ESI_ATTRS	8

struct energy_scale_attribute {
	__be64 id;
	__be64 val;
	u8 desc[64];
	u8 value_desc[64];
} __packed;

struct h_energy_scale_info_hdr {
	__be64 num_attrs;
	__be64 array_offset;
	u8 data_header_version;
} __packed;

struct papr_attr {
	u64 id;
	struct kobj_attribute kobj_attr;
};

struct papr_group {
	struct attribute_group pg;
	struct papr_attr pgattrs[KOBJ_MAX_ATTRS];
};

static struct papr_group *papr_groups;
/* /sys/firmware/papr */
static struct kobject *papr_kobj;
/* /sys/firmware/papr/energy_scale_info */
static struct kobject *esi_kobj;

/*
 * Energy modes can change dynamically hence making a new hcall each time the
 * information needs to be retrieved
 */
static int papr_get_attr(u64 id, struct energy_scale_attribute *esi)
{
	int esi_buf_size = ESI_HDR_SIZE + (CURR_MAX_ESI_ATTRS * ESI_ATTR_SIZE);
	int ret, max_esi_attrs = CURR_MAX_ESI_ATTRS;
	struct energy_scale_attribute *curr_esi;
	struct h_energy_scale_info_hdr *hdr;
	char *buf;

	buf = kmalloc(esi_buf_size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

retry:
	ret = plpar_hcall_norets(H_GET_ENERGY_SCALE_INFO, ESI_FLAGS_SINGLE,
				 id, virt_to_phys(buf),
				 esi_buf_size);

	/*
	 * If the hcall fails with not enough memory for either the
	 * header or data, attempt to allocate more
	 */
	if (ret == H_PARTIAL || ret == H_P4) {
		char *temp_buf;

		max_esi_attrs += 4;
		esi_buf_size = ESI_HDR_SIZE + (CURR_MAX_ESI_ATTRS * max_esi_attrs);

		temp_buf = krealloc(buf, esi_buf_size, GFP_KERNEL);
		if (temp_buf)
			buf = temp_buf;
		else
			return -ENOMEM;

		goto retry;
	}

	if (ret != H_SUCCESS) {
		pr_warn("hcall failed: H_GET_ENERGY_SCALE_INFO");
		ret = -EIO;
		goto out_buf;
	}

	hdr = (struct h_energy_scale_info_hdr *) buf;
	curr_esi = (struct energy_scale_attribute *)
		(buf + be64_to_cpu(hdr->array_offset));

	if (esi_buf_size <
	    be64_to_cpu(hdr->array_offset) + (be64_to_cpu(hdr->num_attrs)
	    * sizeof(struct energy_scale_attribute))) {
		ret = -EIO;
		goto out_buf;
	}

	*esi = *curr_esi;

out_buf:
	kfree(buf);

	return ret;
}

/*
 * Extract and export the description of the energy scale attributes
 */
static ssize_t desc_show(struct kobject *kobj,
			  struct kobj_attribute *kobj_attr,
			  char *buf)
{
	struct papr_attr *pattr = container_of(kobj_attr, struct papr_attr,
					       kobj_attr);
	struct energy_scale_attribute esi;
	int ret;

	ret = papr_get_attr(pattr->id, &esi);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", esi.desc);
}

/*
 * Extract and export the numeric value of the energy scale attributes
 */
static ssize_t val_show(struct kobject *kobj,
			 struct kobj_attribute *kobj_attr,
			 char *buf)
{
	struct papr_attr *pattr = container_of(kobj_attr, struct papr_attr,
					       kobj_attr);
	struct energy_scale_attribute esi;
	int ret;

	ret = papr_get_attr(pattr->id, &esi);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%llu\n", be64_to_cpu(esi.val));
}

/*
 * Extract and export the value description in string format of the energy
 * scale attributes
 */
static ssize_t val_desc_show(struct kobject *kobj,
			      struct kobj_attribute *kobj_attr,
			      char *buf)
{
	struct papr_attr *pattr = container_of(kobj_attr, struct papr_attr,
					       kobj_attr);
	struct energy_scale_attribute esi;
	int ret;

	ret = papr_get_attr(pattr->id, &esi);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", esi.value_desc);
}

static struct papr_ops_info {
	const char *attr_name;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *kobj_attr,
			char *buf);
} ops_info[KOBJ_MAX_ATTRS] = {
	{ "desc", desc_show },
	{ "value", val_show },
	{ "value_desc", val_desc_show },
};

static void add_attr(u64 id, int index, struct papr_attr *attr)
{
	attr->id = id;
	sysfs_attr_init(&attr->kobj_attr.attr);
	attr->kobj_attr.attr.name = ops_info[index].attr_name;
	attr->kobj_attr.attr.mode = 0444;
	attr->kobj_attr.show = ops_info[index].show;
}

static int add_attr_group(u64 id, struct papr_group *pg, bool show_val_desc)
{
	int i;

	for (i = 0; i < KOBJ_MAX_ATTRS; i++) {
		if (!strcmp(ops_info[i].attr_name, "value_desc") &&
		    !show_val_desc) {
			continue;
		}
		add_attr(id, i, &pg->pgattrs[i]);
		pg->pg.attrs[i] = &pg->pgattrs[i].kobj_attr.attr;
	}

	return sysfs_create_group(esi_kobj, &pg->pg);
}


static int __init papr_init(void)
{
	int esi_buf_size = ESI_HDR_SIZE + (CURR_MAX_ESI_ATTRS * ESI_ATTR_SIZE);
	int ret, idx, i, max_esi_attrs = CURR_MAX_ESI_ATTRS;
	struct h_energy_scale_info_hdr *esi_hdr;
	struct energy_scale_attribute *esi_attrs;
	uint64_t num_attrs;
	char *esi_buf;

	if (!firmware_has_feature(FW_FEATURE_LPAR) ||
	    !firmware_has_feature(FW_FEATURE_ENERGY_SCALE_INFO)) {
		return -ENXIO;
	}

	esi_buf = kmalloc(esi_buf_size, GFP_KERNEL);
	if (esi_buf == NULL)
		return -ENOMEM;
	/*
	 * hcall(
	 * uint64 H_GET_ENERGY_SCALE_INFO,  // Get energy scale info
	 * uint64 flags,            // Per the flag request
	 * uint64 firstAttributeId, // The attribute id
	 * uint64 bufferAddress,    // Guest physical address of the output buffer
	 * uint64 bufferSize);      // The size in bytes of the output buffer
	 */
retry:

	ret = plpar_hcall_norets(H_GET_ENERGY_SCALE_INFO, ESI_FLAGS_ALL, 0,
				 virt_to_phys(esi_buf), esi_buf_size);

	/*
	 * If the hcall fails with not enough memory for either the
	 * header or data, attempt to allocate more
	 */
	if (ret == H_PARTIAL || ret == H_P4) {
		char *temp_esi_buf;

		max_esi_attrs += 4;
		esi_buf_size = ESI_HDR_SIZE + (CURR_MAX_ESI_ATTRS * max_esi_attrs);

		temp_esi_buf = krealloc(esi_buf, esi_buf_size, GFP_KERNEL);
		if (temp_esi_buf)
			esi_buf = temp_esi_buf;
		else
			return -ENOMEM;

		goto retry;
	}

	if (ret != H_SUCCESS) {
		pr_warn("hcall failed: H_GET_ENERGY_SCALE_INFO, ret: %d\n", ret);
		goto out_free_esi_buf;
	}

	esi_hdr = (struct h_energy_scale_info_hdr *) esi_buf;
	num_attrs = be64_to_cpu(esi_hdr->num_attrs);
	esi_attrs = (struct energy_scale_attribute *)
		    (esi_buf + be64_to_cpu(esi_hdr->array_offset));

	if (esi_buf_size <
	    be64_to_cpu(esi_hdr->array_offset) +
	    (num_attrs * sizeof(struct energy_scale_attribute))) {
		goto out_free_esi_buf;
	}

	papr_groups = kcalloc(num_attrs, sizeof(*papr_groups), GFP_KERNEL);
	if (!papr_groups)
		goto out_free_esi_buf;

	papr_kobj = kobject_create_and_add("papr", firmware_kobj);
	if (!papr_kobj) {
		pr_warn("kobject_create_and_add papr failed\n");
		goto out_papr_groups;
	}

	esi_kobj = kobject_create_and_add("energy_scale_info", papr_kobj);
	if (!esi_kobj) {
		pr_warn("kobject_create_and_add energy_scale_info failed\n");
		goto out_kobj;
	}

	/* Allocate the groups before registering */
	for (idx = 0; idx < num_attrs; idx++) {
		papr_groups[idx].pg.attrs = kcalloc(KOBJ_MAX_ATTRS + 1,
					    sizeof(*papr_groups[idx].pg.attrs),
					    GFP_KERNEL);
		if (!papr_groups[idx].pg.attrs)
			goto out_pgattrs;

		papr_groups[idx].pg.name = kasprintf(GFP_KERNEL, "%lld",
					     be64_to_cpu(esi_attrs[idx].id));
		if (papr_groups[idx].pg.name == NULL)
			goto out_pgattrs;
	}

	for (idx = 0; idx < num_attrs; idx++) {
		bool show_val_desc = true;

		/* Do not add the value desc attr if it does not exist */
		if (strnlen(esi_attrs[idx].value_desc,
			    sizeof(esi_attrs[idx].value_desc)) == 0)
			show_val_desc = false;

		if (add_attr_group(be64_to_cpu(esi_attrs[idx].id),
				   &papr_groups[idx],
				   show_val_desc)) {
			pr_warn("Failed to create papr attribute group %s\n",
				papr_groups[idx].pg.name);
			idx = num_attrs;
			goto out_pgattrs;
		}
	}

	kfree(esi_buf);
	return 0;
out_pgattrs:
	for (i = 0; i < idx ; i++) {
		kfree(papr_groups[i].pg.attrs);
		kfree(papr_groups[i].pg.name);
	}
	kobject_put(esi_kobj);
out_kobj:
	kobject_put(papr_kobj);
out_papr_groups:
	kfree(papr_groups);
out_free_esi_buf:
	kfree(esi_buf);

	return -ENOMEM;
}

machine_device_initcall(pseries, papr_init);
