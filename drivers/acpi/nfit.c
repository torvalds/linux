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
#include <linux/list_sort.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>
#include <linux/ndctl.h>
#include <linux/list.h>
#include <linux/acpi.h>
#include <linux/sort.h>
#include "nfit.h"

static bool force_enable_dimms;
module_param(force_enable_dimms, bool, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(force_enable_dimms, "Ignore _STA (ACPI DIMM device) status");

static u8 nfit_uuid[NFIT_UUID_MAX][16];

static const u8 *to_nfit_uuid(enum nfit_uuids id)
{
	return nfit_uuid[id];
}

static struct acpi_nfit_desc *to_acpi_nfit_desc(
		struct nvdimm_bus_descriptor *nd_desc)
{
	return container_of(nd_desc, struct acpi_nfit_desc, nd_desc);
}

static struct acpi_device *to_acpi_dev(struct acpi_nfit_desc *acpi_desc)
{
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;

	/*
	 * If provider == 'ACPI.NFIT' we can assume 'dev' is a struct
	 * acpi_device.
	 */
	if (!nd_desc->provider_name
			|| strcmp(nd_desc->provider_name, "ACPI.NFIT") != 0)
		return NULL;

	return to_acpi_device(acpi_desc->dev);
}

static int acpi_nfit_ctl(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd, void *buf,
		unsigned int buf_len)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_nfit_desc(nd_desc);
	const struct nd_cmd_desc *desc = NULL;
	union acpi_object in_obj, in_buf, *out_obj;
	struct device *dev = acpi_desc->dev;
	const char *cmd_name, *dimm_name;
	unsigned long dsm_mask;
	acpi_handle handle;
	const u8 *uuid;
	u32 offset;
	int rc, i;

	if (nvdimm) {
		struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
		struct acpi_device *adev = nfit_mem->adev;

		if (!adev)
			return -ENOTTY;
		dimm_name = dev_name(&adev->dev);
		cmd_name = nvdimm_cmd_name(cmd);
		dsm_mask = nfit_mem->dsm_mask;
		desc = nd_cmd_dimm_desc(cmd);
		uuid = to_nfit_uuid(NFIT_DEV_DIMM);
		handle = adev->handle;
	} else {
		struct acpi_device *adev = to_acpi_dev(acpi_desc);

		cmd_name = nvdimm_bus_cmd_name(cmd);
		dsm_mask = nd_desc->dsm_mask;
		desc = nd_cmd_bus_desc(cmd);
		uuid = to_nfit_uuid(NFIT_DEV_BUS);
		handle = adev->handle;
		dimm_name = "bus";
	}

	if (!desc || (cmd && (desc->out_num + desc->in_num == 0)))
		return -ENOTTY;

	if (!test_bit(cmd, &dsm_mask))
		return -ENOTTY;

	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = 1;
	in_obj.package.elements = &in_buf;
	in_buf.type = ACPI_TYPE_BUFFER;
	in_buf.buffer.pointer = buf;
	in_buf.buffer.length = 0;

	/* libnvdimm has already validated the input envelope */
	for (i = 0; i < desc->in_num; i++)
		in_buf.buffer.length += nd_cmd_in_size(nvdimm, cmd, desc,
				i, buf);

	if (IS_ENABLED(CONFIG_ACPI_NFIT_DEBUG)) {
		dev_dbg(dev, "%s:%s cmd: %s input length: %d\n", __func__,
				dimm_name, cmd_name, in_buf.buffer.length);
		print_hex_dump_debug(cmd_name, DUMP_PREFIX_OFFSET, 4,
				4, in_buf.buffer.pointer, min_t(u32, 128,
					in_buf.buffer.length), true);
	}

	out_obj = acpi_evaluate_dsm(handle, uuid, 1, cmd, &in_obj);
	if (!out_obj) {
		dev_dbg(dev, "%s:%s _DSM failed cmd: %s\n", __func__, dimm_name,
				cmd_name);
		return -EINVAL;
	}

	if (out_obj->package.type != ACPI_TYPE_BUFFER) {
		dev_dbg(dev, "%s:%s unexpected output object type cmd: %s type: %d\n",
				__func__, dimm_name, cmd_name, out_obj->type);
		rc = -EINVAL;
		goto out;
	}

	if (IS_ENABLED(CONFIG_ACPI_NFIT_DEBUG)) {
		dev_dbg(dev, "%s:%s cmd: %s output length: %d\n", __func__,
				dimm_name, cmd_name, out_obj->buffer.length);
		print_hex_dump_debug(cmd_name, DUMP_PREFIX_OFFSET, 4,
				4, out_obj->buffer.pointer, min_t(u32, 128,
					out_obj->buffer.length), true);
	}

	for (i = 0, offset = 0; i < desc->out_num; i++) {
		u32 out_size = nd_cmd_out_size(nvdimm, cmd, desc, i, buf,
				(u32 *) out_obj->buffer.pointer);

		if (offset + out_size > out_obj->buffer.length) {
			dev_dbg(dev, "%s:%s output object underflow cmd: %s field: %d\n",
					__func__, dimm_name, cmd_name, i);
			break;
		}

		if (in_buf.buffer.length + offset + out_size > buf_len) {
			dev_dbg(dev, "%s:%s output overrun cmd: %s field: %d\n",
					__func__, dimm_name, cmd_name, i);
			rc = -ENXIO;
			goto out;
		}
		memcpy(buf + in_buf.buffer.length + offset,
				out_obj->buffer.pointer + offset, out_size);
		offset += out_size;
	}
	if (offset + in_buf.buffer.length < buf_len) {
		if (i >= 1) {
			/*
			 * status valid, return the number of bytes left
			 * unfilled in the output buffer
			 */
			rc = buf_len - offset - in_buf.buffer.length;
		} else {
			dev_err(dev, "%s:%s underrun cmd: %s buf_len: %d out_len: %d\n",
					__func__, dimm_name, cmd_name, buf_len,
					offset);
			rc = -ENXIO;
		}
	} else
		rc = 0;

 out:
	ACPI_FREE(out_obj);

	return rc;
}

static const char *spa_type_name(u16 type)
{
	static const char *to_name[] = {
		[NFIT_SPA_VOLATILE] = "volatile",
		[NFIT_SPA_PM] = "pmem",
		[NFIT_SPA_DCR] = "dimm-control-region",
		[NFIT_SPA_BDW] = "block-data-window",
		[NFIT_SPA_VDISK] = "volatile-disk",
		[NFIT_SPA_VCD] = "volatile-cd",
		[NFIT_SPA_PDISK] = "persistent-disk",
		[NFIT_SPA_PCD] = "persistent-cd",

	};

	if (type > NFIT_SPA_PCD)
		return "unknown";

	return to_name[type];
}

static int nfit_spa_type(struct acpi_nfit_system_address *spa)
{
	int i;

	for (i = 0; i < NFIT_UUID_MAX; i++)
		if (memcmp(to_nfit_uuid(i), spa->range_guid, 16) == 0)
			return i;
	return -1;
}

static bool add_spa(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_system_address *spa)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_spa *nfit_spa = devm_kzalloc(dev, sizeof(*nfit_spa),
			GFP_KERNEL);

	if (!nfit_spa)
		return false;
	INIT_LIST_HEAD(&nfit_spa->list);
	nfit_spa->spa = spa;
	list_add_tail(&nfit_spa->list, &acpi_desc->spas);
	dev_dbg(dev, "%s: spa index: %d type: %s\n", __func__,
			spa->range_index,
			spa_type_name(nfit_spa_type(spa)));
	return true;
}

static bool add_memdev(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_memory_map *memdev)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_memdev *nfit_memdev = devm_kzalloc(dev,
			sizeof(*nfit_memdev), GFP_KERNEL);

	if (!nfit_memdev)
		return false;
	INIT_LIST_HEAD(&nfit_memdev->list);
	nfit_memdev->memdev = memdev;
	list_add_tail(&nfit_memdev->list, &acpi_desc->memdevs);
	dev_dbg(dev, "%s: memdev handle: %#x spa: %d dcr: %d\n",
			__func__, memdev->device_handle, memdev->range_index,
			memdev->region_index);
	return true;
}

static bool add_dcr(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_control_region *dcr)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_dcr *nfit_dcr = devm_kzalloc(dev, sizeof(*nfit_dcr),
			GFP_KERNEL);

	if (!nfit_dcr)
		return false;
	INIT_LIST_HEAD(&nfit_dcr->list);
	nfit_dcr->dcr = dcr;
	list_add_tail(&nfit_dcr->list, &acpi_desc->dcrs);
	dev_dbg(dev, "%s: dcr index: %d windows: %d\n", __func__,
			dcr->region_index, dcr->windows);
	return true;
}

static bool add_bdw(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_data_region *bdw)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_bdw *nfit_bdw = devm_kzalloc(dev, sizeof(*nfit_bdw),
			GFP_KERNEL);

	if (!nfit_bdw)
		return false;
	INIT_LIST_HEAD(&nfit_bdw->list);
	nfit_bdw->bdw = bdw;
	list_add_tail(&nfit_bdw->list, &acpi_desc->bdws);
	dev_dbg(dev, "%s: bdw dcr: %d windows: %d\n", __func__,
			bdw->region_index, bdw->windows);
	return true;
}

static void *add_table(struct acpi_nfit_desc *acpi_desc, void *table,
		const void *end)
{
	struct device *dev = acpi_desc->dev;
	struct acpi_nfit_header *hdr;
	void *err = ERR_PTR(-ENOMEM);

	if (table >= end)
		return NULL;

	hdr = table;
	switch (hdr->type) {
	case ACPI_NFIT_TYPE_SYSTEM_ADDRESS:
		if (!add_spa(acpi_desc, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_MEMORY_MAP:
		if (!add_memdev(acpi_desc, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_CONTROL_REGION:
		if (!add_dcr(acpi_desc, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_DATA_REGION:
		if (!add_bdw(acpi_desc, table))
			return err;
		break;
	/* TODO */
	case ACPI_NFIT_TYPE_INTERLEAVE:
		dev_dbg(dev, "%s: idt\n", __func__);
		break;
	case ACPI_NFIT_TYPE_FLUSH_ADDRESS:
		dev_dbg(dev, "%s: flush\n", __func__);
		break;
	case ACPI_NFIT_TYPE_SMBIOS:
		dev_dbg(dev, "%s: smbios\n", __func__);
		break;
	default:
		dev_err(dev, "unknown table '%d' parsing nfit\n", hdr->type);
		break;
	}

	return table + hdr->length;
}

static void nfit_mem_find_spa_bdw(struct acpi_nfit_desc *acpi_desc,
		struct nfit_mem *nfit_mem)
{
	u32 device_handle = __to_nfit_memdev(nfit_mem)->device_handle;
	u16 dcr = nfit_mem->dcr->region_index;
	struct nfit_spa *nfit_spa;

	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		u16 range_index = nfit_spa->spa->range_index;
		int type = nfit_spa_type(nfit_spa->spa);
		struct nfit_memdev *nfit_memdev;

		if (type != NFIT_SPA_BDW)
			continue;

		list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
			if (nfit_memdev->memdev->range_index != range_index)
				continue;
			if (nfit_memdev->memdev->device_handle != device_handle)
				continue;
			if (nfit_memdev->memdev->region_index != dcr)
				continue;

			nfit_mem->spa_bdw = nfit_spa->spa;
			return;
		}
	}

	dev_dbg(acpi_desc->dev, "SPA-BDW not found for SPA-DCR %d\n",
			nfit_mem->spa_dcr->range_index);
	nfit_mem->bdw = NULL;
}

static int nfit_mem_add(struct acpi_nfit_desc *acpi_desc,
		struct nfit_mem *nfit_mem, struct acpi_nfit_system_address *spa)
{
	u16 dcr = __to_nfit_memdev(nfit_mem)->region_index;
	struct nfit_dcr *nfit_dcr;
	struct nfit_bdw *nfit_bdw;

	list_for_each_entry(nfit_dcr, &acpi_desc->dcrs, list) {
		if (nfit_dcr->dcr->region_index != dcr)
			continue;
		nfit_mem->dcr = nfit_dcr->dcr;
		break;
	}

	if (!nfit_mem->dcr) {
		dev_dbg(acpi_desc->dev, "SPA %d missing:%s%s\n",
				spa->range_index, __to_nfit_memdev(nfit_mem)
				? "" : " MEMDEV", nfit_mem->dcr ? "" : " DCR");
		return -ENODEV;
	}

	/*
	 * We've found enough to create an nvdimm, optionally
	 * find an associated BDW
	 */
	list_add(&nfit_mem->list, &acpi_desc->dimms);

	list_for_each_entry(nfit_bdw, &acpi_desc->bdws, list) {
		if (nfit_bdw->bdw->region_index != dcr)
			continue;
		nfit_mem->bdw = nfit_bdw->bdw;
		break;
	}

	if (!nfit_mem->bdw)
		return 0;

	nfit_mem_find_spa_bdw(acpi_desc, nfit_mem);
	return 0;
}

static int nfit_mem_dcr_init(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_system_address *spa)
{
	struct nfit_mem *nfit_mem, *found;
	struct nfit_memdev *nfit_memdev;
	int type = nfit_spa_type(spa);
	u16 dcr;

	switch (type) {
	case NFIT_SPA_DCR:
	case NFIT_SPA_PM:
		break;
	default:
		return 0;
	}

	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
		int rc;

		if (nfit_memdev->memdev->range_index != spa->range_index)
			continue;
		found = NULL;
		dcr = nfit_memdev->memdev->region_index;
		list_for_each_entry(nfit_mem, &acpi_desc->dimms, list)
			if (__to_nfit_memdev(nfit_mem)->region_index == dcr) {
				found = nfit_mem;
				break;
			}

		if (found)
			nfit_mem = found;
		else {
			nfit_mem = devm_kzalloc(acpi_desc->dev,
					sizeof(*nfit_mem), GFP_KERNEL);
			if (!nfit_mem)
				return -ENOMEM;
			INIT_LIST_HEAD(&nfit_mem->list);
		}

		if (type == NFIT_SPA_DCR) {
			/* multiple dimms may share a SPA when interleaved */
			nfit_mem->spa_dcr = spa;
			nfit_mem->memdev_dcr = nfit_memdev->memdev;
		} else {
			/*
			 * A single dimm may belong to multiple SPA-PM
			 * ranges, record at least one in addition to
			 * any SPA-DCR range.
			 */
			nfit_mem->memdev_pmem = nfit_memdev->memdev;
		}

		if (found)
			continue;

		rc = nfit_mem_add(acpi_desc, nfit_mem, spa);
		if (rc)
			return rc;
	}

	return 0;
}

static int nfit_mem_cmp(void *priv, struct list_head *_a, struct list_head *_b)
{
	struct nfit_mem *a = container_of(_a, typeof(*a), list);
	struct nfit_mem *b = container_of(_b, typeof(*b), list);
	u32 handleA, handleB;

	handleA = __to_nfit_memdev(a)->device_handle;
	handleB = __to_nfit_memdev(b)->device_handle;
	if (handleA < handleB)
		return -1;
	else if (handleA > handleB)
		return 1;
	return 0;
}

static int nfit_mem_init(struct acpi_nfit_desc *acpi_desc)
{
	struct nfit_spa *nfit_spa;

	/*
	 * For each SPA-DCR or SPA-PMEM address range find its
	 * corresponding MEMDEV(s).  From each MEMDEV find the
	 * corresponding DCR.  Then, if we're operating on a SPA-DCR,
	 * try to find a SPA-BDW and a corresponding BDW that references
	 * the DCR.  Throw it all into an nfit_mem object.  Note, that
	 * BDWs are optional.
	 */
	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		int rc;

		rc = nfit_mem_dcr_init(acpi_desc, nfit_spa->spa);
		if (rc)
			return rc;
	}

	list_sort(NULL, &acpi_desc->dimms, nfit_mem_cmp);

	return 0;
}

static ssize_t revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	return sprintf(buf, "%d\n", acpi_desc->nfit->header.revision);
}
static DEVICE_ATTR_RO(revision);

static struct attribute *acpi_nfit_attributes[] = {
	&dev_attr_revision.attr,
	NULL,
};

static struct attribute_group acpi_nfit_attribute_group = {
	.name = "nfit",
	.attrs = acpi_nfit_attributes,
};

static const struct attribute_group *acpi_nfit_attribute_groups[] = {
	&nvdimm_bus_attribute_group,
	&acpi_nfit_attribute_group,
	NULL,
};

static struct acpi_nfit_memory_map *to_nfit_memdev(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	return __to_nfit_memdev(nfit_mem);
}

static struct acpi_nfit_control_region *to_nfit_dcr(struct device *dev)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	return nfit_mem->dcr;
}

static ssize_t handle_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_memory_map *memdev = to_nfit_memdev(dev);

	return sprintf(buf, "%#x\n", memdev->device_handle);
}
static DEVICE_ATTR_RO(handle);

static ssize_t phys_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_memory_map *memdev = to_nfit_memdev(dev);

	return sprintf(buf, "%#x\n", memdev->physical_id);
}
static DEVICE_ATTR_RO(phys_id);

static ssize_t vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "%#x\n", dcr->vendor_id);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t rev_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "%#x\n", dcr->revision_id);
}
static DEVICE_ATTR_RO(rev_id);

static ssize_t device_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "%#x\n", dcr->device_id);
}
static DEVICE_ATTR_RO(device);

static ssize_t format_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "%#x\n", dcr->code);
}
static DEVICE_ATTR_RO(format);

static ssize_t serial_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "%#x\n", dcr->serial_number);
}
static DEVICE_ATTR_RO(serial);

static struct attribute *acpi_nfit_dimm_attributes[] = {
	&dev_attr_handle.attr,
	&dev_attr_phys_id.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_format.attr,
	&dev_attr_serial.attr,
	&dev_attr_rev_id.attr,
	NULL,
};

static umode_t acpi_nfit_dimm_attr_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);

	if (to_nfit_dcr(dev))
		return a->mode;
	else
		return 0;
}

static struct attribute_group acpi_nfit_dimm_attribute_group = {
	.name = "nfit",
	.attrs = acpi_nfit_dimm_attributes,
	.is_visible = acpi_nfit_dimm_attr_visible,
};

static const struct attribute_group *acpi_nfit_dimm_attribute_groups[] = {
	&nvdimm_attribute_group,
	&nd_device_attribute_group,
	&acpi_nfit_dimm_attribute_group,
	NULL,
};

static struct nvdimm *acpi_nfit_dimm_by_handle(struct acpi_nfit_desc *acpi_desc,
		u32 device_handle)
{
	struct nfit_mem *nfit_mem;

	list_for_each_entry(nfit_mem, &acpi_desc->dimms, list)
		if (__to_nfit_memdev(nfit_mem)->device_handle == device_handle)
			return nfit_mem->nvdimm;

	return NULL;
}

static int acpi_nfit_add_dimm(struct acpi_nfit_desc *acpi_desc,
		struct nfit_mem *nfit_mem, u32 device_handle)
{
	struct acpi_device *adev, *adev_dimm;
	struct device *dev = acpi_desc->dev;
	const u8 *uuid = to_nfit_uuid(NFIT_DEV_DIMM);
	unsigned long long sta;
	int i, rc = -ENODEV;
	acpi_status status;

	nfit_mem->dsm_mask = acpi_desc->dimm_dsm_force_en;
	adev = to_acpi_dev(acpi_desc);
	if (!adev)
		return 0;

	adev_dimm = acpi_find_child_device(adev, device_handle, false);
	nfit_mem->adev = adev_dimm;
	if (!adev_dimm) {
		dev_err(dev, "no ACPI.NFIT device with _ADR %#x, disabling...\n",
				device_handle);
		return force_enable_dimms ? 0 : -ENODEV;
	}

	status = acpi_evaluate_integer(adev_dimm->handle, "_STA", NULL, &sta);
	if (status == AE_NOT_FOUND) {
		dev_dbg(dev, "%s missing _STA, assuming enabled...\n",
				dev_name(&adev_dimm->dev));
		rc = 0;
	} else if (ACPI_FAILURE(status))
		dev_err(dev, "%s failed to retrieve_STA, disabling...\n",
				dev_name(&adev_dimm->dev));
	else if ((sta & ACPI_STA_DEVICE_ENABLED) == 0)
		dev_info(dev, "%s disabled by firmware\n",
				dev_name(&adev_dimm->dev));
	else
		rc = 0;

	for (i = ND_CMD_SMART; i <= ND_CMD_VENDOR; i++)
		if (acpi_check_dsm(adev_dimm->handle, uuid, 1, 1ULL << i))
			set_bit(i, &nfit_mem->dsm_mask);

	return force_enable_dimms ? 0 : rc;
}

static int acpi_nfit_register_dimms(struct acpi_nfit_desc *acpi_desc)
{
	struct nfit_mem *nfit_mem;
	int dimm_count = 0;

	list_for_each_entry(nfit_mem, &acpi_desc->dimms, list) {
		struct nvdimm *nvdimm;
		unsigned long flags = 0;
		u32 device_handle;
		int rc;

		device_handle = __to_nfit_memdev(nfit_mem)->device_handle;
		nvdimm = acpi_nfit_dimm_by_handle(acpi_desc, device_handle);
		if (nvdimm) {
			/*
			 * If for some reason we find multiple DCRs the
			 * first one wins
			 */
			dev_err(acpi_desc->dev, "duplicate DCR detected: %s\n",
					nvdimm_name(nvdimm));
			continue;
		}

		if (nfit_mem->bdw && nfit_mem->memdev_pmem)
			flags |= NDD_ALIASING;

		rc = acpi_nfit_add_dimm(acpi_desc, nfit_mem, device_handle);
		if (rc)
			continue;

		nvdimm = nvdimm_create(acpi_desc->nvdimm_bus, nfit_mem,
				acpi_nfit_dimm_attribute_groups,
				flags, &nfit_mem->dsm_mask);
		if (!nvdimm)
			return -ENOMEM;

		nfit_mem->nvdimm = nvdimm;
		dimm_count++;
	}

	return nvdimm_bus_check_dimm_count(acpi_desc->nvdimm_bus, dimm_count);
}

static void acpi_nfit_init_dsms(struct acpi_nfit_desc *acpi_desc)
{
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;
	const u8 *uuid = to_nfit_uuid(NFIT_DEV_BUS);
	struct acpi_device *adev;
	int i;

	adev = to_acpi_dev(acpi_desc);
	if (!adev)
		return;

	for (i = ND_CMD_ARS_CAP; i <= ND_CMD_ARS_STATUS; i++)
		if (acpi_check_dsm(adev->handle, uuid, 1, 1ULL << i))
			set_bit(i, &nd_desc->dsm_mask);
}

static ssize_t range_index_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	struct nfit_spa *nfit_spa = nd_region_provider_data(nd_region);

	return sprintf(buf, "%d\n", nfit_spa->spa->range_index);
}
static DEVICE_ATTR_RO(range_index);

static struct attribute *acpi_nfit_region_attributes[] = {
	&dev_attr_range_index.attr,
	NULL,
};

static struct attribute_group acpi_nfit_region_attribute_group = {
	.name = "nfit",
	.attrs = acpi_nfit_region_attributes,
};

static const struct attribute_group *acpi_nfit_region_attribute_groups[] = {
	&nd_region_attribute_group,
	&nd_mapping_attribute_group,
	&nd_device_attribute_group,
	&acpi_nfit_region_attribute_group,
	NULL,
};

/* enough info to uniquely specify an interleave set */
struct nfit_set_info {
	struct nfit_set_info_map {
		u64 region_offset;
		u32 serial_number;
		u32 pad;
	} mapping[0];
};

static size_t sizeof_nfit_set_info(int num_mappings)
{
	return sizeof(struct nfit_set_info)
		+ num_mappings * sizeof(struct nfit_set_info_map);
}

static int cmp_map(const void *m0, const void *m1)
{
	const struct nfit_set_info_map *map0 = m0;
	const struct nfit_set_info_map *map1 = m1;

	return memcmp(&map0->region_offset, &map1->region_offset,
			sizeof(u64));
}

/* Retrieve the nth entry referencing this spa */
static struct acpi_nfit_memory_map *memdev_from_spa(
		struct acpi_nfit_desc *acpi_desc, u16 range_index, int n)
{
	struct nfit_memdev *nfit_memdev;

	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list)
		if (nfit_memdev->memdev->range_index == range_index)
			if (n-- == 0)
				return nfit_memdev->memdev;
	return NULL;
}

static int acpi_nfit_init_interleave_set(struct acpi_nfit_desc *acpi_desc,
		struct nd_region_desc *ndr_desc,
		struct acpi_nfit_system_address *spa)
{
	int i, spa_type = nfit_spa_type(spa);
	struct device *dev = acpi_desc->dev;
	struct nd_interleave_set *nd_set;
	u16 nr = ndr_desc->num_mappings;
	struct nfit_set_info *info;

	if (spa_type == NFIT_SPA_PM || spa_type == NFIT_SPA_VOLATILE)
		/* pass */;
	else
		return 0;

	nd_set = devm_kzalloc(dev, sizeof(*nd_set), GFP_KERNEL);
	if (!nd_set)
		return -ENOMEM;

	info = devm_kzalloc(dev, sizeof_nfit_set_info(nr), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	for (i = 0; i < nr; i++) {
		struct nd_mapping *nd_mapping = &ndr_desc->nd_mapping[i];
		struct nfit_set_info_map *map = &info->mapping[i];
		struct nvdimm *nvdimm = nd_mapping->nvdimm;
		struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
		struct acpi_nfit_memory_map *memdev = memdev_from_spa(acpi_desc,
				spa->range_index, i);

		if (!memdev || !nfit_mem->dcr) {
			dev_err(dev, "%s: failed to find DCR\n", __func__);
			return -ENODEV;
		}

		map->region_offset = memdev->region_offset;
		map->serial_number = nfit_mem->dcr->serial_number;
	}

	sort(&info->mapping[0], nr, sizeof(struct nfit_set_info_map),
			cmp_map, NULL);
	nd_set->cookie = nd_fletcher64(info, sizeof_nfit_set_info(nr), 0);
	ndr_desc->nd_set = nd_set;
	devm_kfree(dev, info);

	return 0;
}

static int acpi_nfit_init_mapping(struct acpi_nfit_desc *acpi_desc,
		struct nd_mapping *nd_mapping, struct nd_region_desc *ndr_desc,
		struct acpi_nfit_memory_map *memdev,
		struct acpi_nfit_system_address *spa)
{
	struct nvdimm *nvdimm = acpi_nfit_dimm_by_handle(acpi_desc,
			memdev->device_handle);
	struct nfit_mem *nfit_mem;
	int blk_valid = 0;

	if (!nvdimm) {
		dev_err(acpi_desc->dev, "spa%d dimm: %#x not found\n",
				spa->range_index, memdev->device_handle);
		return -ENODEV;
	}

	nd_mapping->nvdimm = nvdimm;
	switch (nfit_spa_type(spa)) {
	case NFIT_SPA_PM:
	case NFIT_SPA_VOLATILE:
		nd_mapping->start = memdev->address;
		nd_mapping->size = memdev->region_size;
		break;
	case NFIT_SPA_DCR:
		nfit_mem = nvdimm_provider_data(nvdimm);
		if (!nfit_mem || !nfit_mem->bdw) {
			dev_dbg(acpi_desc->dev, "spa%d %s missing bdw\n",
					spa->range_index, nvdimm_name(nvdimm));
		} else {
			nd_mapping->size = nfit_mem->bdw->capacity;
			nd_mapping->start = nfit_mem->bdw->start_address;
			blk_valid = 1;
		}

		ndr_desc->nd_mapping = nd_mapping;
		ndr_desc->num_mappings = blk_valid;
		if (!nvdimm_blk_region_create(acpi_desc->nvdimm_bus, ndr_desc))
			return -ENOMEM;
		break;
	}

	return 0;
}

static int acpi_nfit_register_region(struct acpi_nfit_desc *acpi_desc,
		struct nfit_spa *nfit_spa)
{
	static struct nd_mapping nd_mappings[ND_MAX_MAPPINGS];
	struct acpi_nfit_system_address *spa = nfit_spa->spa;
	struct nfit_memdev *nfit_memdev;
	struct nd_region_desc ndr_desc;
	struct nvdimm_bus *nvdimm_bus;
	struct resource res;
	int count = 0, rc;

	if (spa->range_index == 0) {
		dev_dbg(acpi_desc->dev, "%s: detected invalid spa index\n",
				__func__);
		return 0;
	}

	memset(&res, 0, sizeof(res));
	memset(&nd_mappings, 0, sizeof(nd_mappings));
	memset(&ndr_desc, 0, sizeof(ndr_desc));
	res.start = spa->address;
	res.end = res.start + spa->length - 1;
	ndr_desc.res = &res;
	ndr_desc.provider_data = nfit_spa;
	ndr_desc.attr_groups = acpi_nfit_region_attribute_groups;
	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
		struct acpi_nfit_memory_map *memdev = nfit_memdev->memdev;
		struct nd_mapping *nd_mapping;

		if (memdev->range_index != spa->range_index)
			continue;
		if (count >= ND_MAX_MAPPINGS) {
			dev_err(acpi_desc->dev, "spa%d exceeds max mappings %d\n",
					spa->range_index, ND_MAX_MAPPINGS);
			return -ENXIO;
		}
		nd_mapping = &nd_mappings[count++];
		rc = acpi_nfit_init_mapping(acpi_desc, nd_mapping, &ndr_desc,
				memdev, spa);
		if (rc)
			return rc;
	}

	ndr_desc.nd_mapping = nd_mappings;
	ndr_desc.num_mappings = count;
	rc = acpi_nfit_init_interleave_set(acpi_desc, &ndr_desc, spa);
	if (rc)
		return rc;

	nvdimm_bus = acpi_desc->nvdimm_bus;
	if (nfit_spa_type(spa) == NFIT_SPA_PM) {
		if (!nvdimm_pmem_region_create(nvdimm_bus, &ndr_desc))
			return -ENOMEM;
	} else if (nfit_spa_type(spa) == NFIT_SPA_VOLATILE) {
		if (!nvdimm_volatile_region_create(nvdimm_bus, &ndr_desc))
			return -ENOMEM;
	}
	return 0;
}

static int acpi_nfit_register_regions(struct acpi_nfit_desc *acpi_desc)
{
	struct nfit_spa *nfit_spa;

	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		int rc = acpi_nfit_register_region(acpi_desc, nfit_spa);

		if (rc)
			return rc;
	}
	return 0;
}

static int acpi_nfit_init(struct acpi_nfit_desc *acpi_desc, acpi_size sz)
{
	struct device *dev = acpi_desc->dev;
	const void *end;
	u8 *data;
	int rc;

	INIT_LIST_HEAD(&acpi_desc->spas);
	INIT_LIST_HEAD(&acpi_desc->dcrs);
	INIT_LIST_HEAD(&acpi_desc->bdws);
	INIT_LIST_HEAD(&acpi_desc->memdevs);
	INIT_LIST_HEAD(&acpi_desc->dimms);

	data = (u8 *) acpi_desc->nfit;
	end = data + sz;
	data += sizeof(struct acpi_table_nfit);
	while (!IS_ERR_OR_NULL(data))
		data = add_table(acpi_desc, data, end);

	if (IS_ERR(data)) {
		dev_dbg(dev, "%s: nfit table parsing error: %ld\n", __func__,
				PTR_ERR(data));
		return PTR_ERR(data);
	}

	if (nfit_mem_init(acpi_desc) != 0)
		return -ENOMEM;

	acpi_nfit_init_dsms(acpi_desc);

	rc = acpi_nfit_register_dimms(acpi_desc);
	if (rc)
		return rc;

	return acpi_nfit_register_regions(acpi_desc);
}

static int acpi_nfit_add(struct acpi_device *adev)
{
	struct nvdimm_bus_descriptor *nd_desc;
	struct acpi_nfit_desc *acpi_desc;
	struct device *dev = &adev->dev;
	struct acpi_table_header *tbl;
	acpi_status status = AE_OK;
	acpi_size sz;
	int rc;

	status = acpi_get_table_with_size("NFIT", 0, &tbl, &sz);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "failed to find NFIT\n");
		return -ENXIO;
	}

	acpi_desc = devm_kzalloc(dev, sizeof(*acpi_desc), GFP_KERNEL);
	if (!acpi_desc)
		return -ENOMEM;

	dev_set_drvdata(dev, acpi_desc);
	acpi_desc->dev = dev;
	acpi_desc->nfit = (struct acpi_table_nfit *) tbl;
	nd_desc = &acpi_desc->nd_desc;
	nd_desc->provider_name = "ACPI.NFIT";
	nd_desc->ndctl = acpi_nfit_ctl;
	nd_desc->attr_groups = acpi_nfit_attribute_groups;

	acpi_desc->nvdimm_bus = nvdimm_bus_register(dev, nd_desc);
	if (!acpi_desc->nvdimm_bus)
		return -ENXIO;

	rc = acpi_nfit_init(acpi_desc, sz);
	if (rc) {
		nvdimm_bus_unregister(acpi_desc->nvdimm_bus);
		return rc;
	}
	return 0;
}

static int acpi_nfit_remove(struct acpi_device *adev)
{
	struct acpi_nfit_desc *acpi_desc = dev_get_drvdata(&adev->dev);

	nvdimm_bus_unregister(acpi_desc->nvdimm_bus);
	return 0;
}

static const struct acpi_device_id acpi_nfit_ids[] = {
	{ "ACPI0012", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, acpi_nfit_ids);

static struct acpi_driver acpi_nfit_driver = {
	.name = KBUILD_MODNAME,
	.ids = acpi_nfit_ids,
	.ops = {
		.add = acpi_nfit_add,
		.remove = acpi_nfit_remove,
	},
};

static __init int nfit_init(void)
{
	BUILD_BUG_ON(sizeof(struct acpi_table_nfit) != 40);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_system_address) != 56);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_memory_map) != 48);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_interleave) != 20);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_smbios) != 9);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_control_region) != 80);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_data_region) != 40);

	acpi_str_to_uuid(UUID_VOLATILE_MEMORY, nfit_uuid[NFIT_SPA_VOLATILE]);
	acpi_str_to_uuid(UUID_PERSISTENT_MEMORY, nfit_uuid[NFIT_SPA_PM]);
	acpi_str_to_uuid(UUID_CONTROL_REGION, nfit_uuid[NFIT_SPA_DCR]);
	acpi_str_to_uuid(UUID_DATA_REGION, nfit_uuid[NFIT_SPA_BDW]);
	acpi_str_to_uuid(UUID_VOLATILE_VIRTUAL_DISK, nfit_uuid[NFIT_SPA_VDISK]);
	acpi_str_to_uuid(UUID_VOLATILE_VIRTUAL_CD, nfit_uuid[NFIT_SPA_VCD]);
	acpi_str_to_uuid(UUID_PERSISTENT_VIRTUAL_DISK, nfit_uuid[NFIT_SPA_PDISK]);
	acpi_str_to_uuid(UUID_PERSISTENT_VIRTUAL_CD, nfit_uuid[NFIT_SPA_PCD]);
	acpi_str_to_uuid(UUID_NFIT_BUS, nfit_uuid[NFIT_DEV_BUS]);
	acpi_str_to_uuid(UUID_NFIT_DIMM, nfit_uuid[NFIT_DEV_DIMM]);

	return acpi_bus_register_driver(&acpi_nfit_driver);
}

static __exit void nfit_exit(void)
{
	acpi_bus_unregister_driver(&acpi_nfit_driver);
}

module_init(nfit_init);
module_exit(nfit_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
