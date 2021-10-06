// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 */
#include <linux/list_sort.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/mutex.h>
#include <linux/ndctl.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/acpi.h>
#include <linux/sort.h>
#include <linux/io.h>
#include <linux/nd.h>
#include <asm/cacheflush.h>
#include <acpi/nfit.h>
#include "intel.h"
#include "nfit.h"

/*
 * For readq() and writeq() on 32-bit builds, the hi-lo, lo-hi order is
 * irrelevant.
 */
#include <linux/io-64-nonatomic-hi-lo.h>

static bool force_enable_dimms;
module_param(force_enable_dimms, bool, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(force_enable_dimms, "Ignore _STA (ACPI DIMM device) status");

static bool disable_vendor_specific;
module_param(disable_vendor_specific, bool, S_IRUGO);
MODULE_PARM_DESC(disable_vendor_specific,
		"Limit commands to the publicly specified set");

static unsigned long override_dsm_mask;
module_param(override_dsm_mask, ulong, S_IRUGO);
MODULE_PARM_DESC(override_dsm_mask, "Bitmask of allowed NVDIMM DSM functions");

static int default_dsm_family = -1;
module_param(default_dsm_family, int, S_IRUGO);
MODULE_PARM_DESC(default_dsm_family,
		"Try this DSM type first when identifying NVDIMM family");

static bool no_init_ars;
module_param(no_init_ars, bool, 0644);
MODULE_PARM_DESC(no_init_ars, "Skip ARS run at nfit init time");

static bool force_labels;
module_param(force_labels, bool, 0444);
MODULE_PARM_DESC(force_labels, "Opt-in to labels despite missing methods");

LIST_HEAD(acpi_descs);
DEFINE_MUTEX(acpi_desc_lock);

static struct workqueue_struct *nfit_wq;

struct nfit_table_prev {
	struct list_head spas;
	struct list_head memdevs;
	struct list_head dcrs;
	struct list_head bdws;
	struct list_head idts;
	struct list_head flushes;
};

static guid_t nfit_uuid[NFIT_UUID_MAX];

const guid_t *to_nfit_uuid(enum nfit_uuids id)
{
	return &nfit_uuid[id];
}
EXPORT_SYMBOL(to_nfit_uuid);

static const guid_t *to_nfit_bus_uuid(int family)
{
	if (WARN_ONCE(family == NVDIMM_BUS_FAMILY_NFIT,
			"only secondary bus families can be translated\n"))
		return NULL;
	/*
	 * The index of bus UUIDs starts immediately following the last
	 * NVDIMM/leaf family.
	 */
	return to_nfit_uuid(family + NVDIMM_FAMILY_MAX);
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

static int xlat_bus_status(void *buf, unsigned int cmd, u32 status)
{
	struct nd_cmd_clear_error *clear_err;
	struct nd_cmd_ars_status *ars_status;
	u16 flags;

	switch (cmd) {
	case ND_CMD_ARS_CAP:
		if ((status & 0xffff) == NFIT_ARS_CAP_NONE)
			return -ENOTTY;

		/* Command failed */
		if (status & 0xffff)
			return -EIO;

		/* No supported scan types for this range */
		flags = ND_ARS_PERSISTENT | ND_ARS_VOLATILE;
		if ((status >> 16 & flags) == 0)
			return -ENOTTY;
		return 0;
	case ND_CMD_ARS_START:
		/* ARS is in progress */
		if ((status & 0xffff) == NFIT_ARS_START_BUSY)
			return -EBUSY;

		/* Command failed */
		if (status & 0xffff)
			return -EIO;
		return 0;
	case ND_CMD_ARS_STATUS:
		ars_status = buf;
		/* Command failed */
		if (status & 0xffff)
			return -EIO;
		/* Check extended status (Upper two bytes) */
		if (status == NFIT_ARS_STATUS_DONE)
			return 0;

		/* ARS is in progress */
		if (status == NFIT_ARS_STATUS_BUSY)
			return -EBUSY;

		/* No ARS performed for the current boot */
		if (status == NFIT_ARS_STATUS_NONE)
			return -EAGAIN;

		/*
		 * ARS interrupted, either we overflowed or some other
		 * agent wants the scan to stop.  If we didn't overflow
		 * then just continue with the returned results.
		 */
		if (status == NFIT_ARS_STATUS_INTR) {
			if (ars_status->out_length >= 40 && (ars_status->flags
						& NFIT_ARS_F_OVERFLOW))
				return -ENOSPC;
			return 0;
		}

		/* Unknown status */
		if (status >> 16)
			return -EIO;
		return 0;
	case ND_CMD_CLEAR_ERROR:
		clear_err = buf;
		if (status & 0xffff)
			return -EIO;
		if (!clear_err->cleared)
			return -EIO;
		if (clear_err->length > clear_err->cleared)
			return clear_err->cleared;
		return 0;
	default:
		break;
	}

	/* all other non-zero status results in an error */
	if (status)
		return -EIO;
	return 0;
}

#define ACPI_LABELS_LOCKED 3

static int xlat_nvdimm_status(struct nvdimm *nvdimm, void *buf, unsigned int cmd,
		u32 status)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		/*
		 * In the _LSI, _LSR, _LSW case the locked status is
		 * communicated via the read/write commands
		 */
		if (test_bit(NFIT_MEM_LSR, &nfit_mem->flags))
			break;

		if (status >> 16 & ND_CONFIG_LOCKED)
			return -EACCES;
		break;
	case ND_CMD_GET_CONFIG_DATA:
		if (test_bit(NFIT_MEM_LSR, &nfit_mem->flags)
				&& status == ACPI_LABELS_LOCKED)
			return -EACCES;
		break;
	case ND_CMD_SET_CONFIG_DATA:
		if (test_bit(NFIT_MEM_LSW, &nfit_mem->flags)
				&& status == ACPI_LABELS_LOCKED)
			return -EACCES;
		break;
	default:
		break;
	}

	/* all other non-zero status results in an error */
	if (status)
		return -EIO;
	return 0;
}

static int xlat_status(struct nvdimm *nvdimm, void *buf, unsigned int cmd,
		u32 status)
{
	if (!nvdimm)
		return xlat_bus_status(buf, cmd, status);
	return xlat_nvdimm_status(nvdimm, buf, cmd, status);
}

/* convert _LS{I,R} packages to the buffer object acpi_nfit_ctl expects */
static union acpi_object *pkg_to_buf(union acpi_object *pkg)
{
	int i;
	void *dst;
	size_t size = 0;
	union acpi_object *buf = NULL;

	if (pkg->type != ACPI_TYPE_PACKAGE) {
		WARN_ONCE(1, "BIOS bug, unexpected element type: %d\n",
				pkg->type);
		goto err;
	}

	for (i = 0; i < pkg->package.count; i++) {
		union acpi_object *obj = &pkg->package.elements[i];

		if (obj->type == ACPI_TYPE_INTEGER)
			size += 4;
		else if (obj->type == ACPI_TYPE_BUFFER)
			size += obj->buffer.length;
		else {
			WARN_ONCE(1, "BIOS bug, unexpected element type: %d\n",
					obj->type);
			goto err;
		}
	}

	buf = ACPI_ALLOCATE(sizeof(*buf) + size);
	if (!buf)
		goto err;

	dst = buf + 1;
	buf->type = ACPI_TYPE_BUFFER;
	buf->buffer.length = size;
	buf->buffer.pointer = dst;
	for (i = 0; i < pkg->package.count; i++) {
		union acpi_object *obj = &pkg->package.elements[i];

		if (obj->type == ACPI_TYPE_INTEGER) {
			memcpy(dst, &obj->integer.value, 4);
			dst += 4;
		} else if (obj->type == ACPI_TYPE_BUFFER) {
			memcpy(dst, obj->buffer.pointer, obj->buffer.length);
			dst += obj->buffer.length;
		}
	}
err:
	ACPI_FREE(pkg);
	return buf;
}

static union acpi_object *int_to_buf(union acpi_object *integer)
{
	union acpi_object *buf = NULL;
	void *dst = NULL;

	if (integer->type != ACPI_TYPE_INTEGER) {
		WARN_ONCE(1, "BIOS bug, unexpected element type: %d\n",
				integer->type);
		goto err;
	}

	buf = ACPI_ALLOCATE(sizeof(*buf) + 4);
	if (!buf)
		goto err;

	dst = buf + 1;
	buf->type = ACPI_TYPE_BUFFER;
	buf->buffer.length = 4;
	buf->buffer.pointer = dst;
	memcpy(dst, &integer->integer.value, 4);
err:
	ACPI_FREE(integer);
	return buf;
}

static union acpi_object *acpi_label_write(acpi_handle handle, u32 offset,
		u32 len, void *data)
{
	acpi_status rc;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input = {
		.count = 3,
		.pointer = (union acpi_object []) {
			[0] = {
				.integer.type = ACPI_TYPE_INTEGER,
				.integer.value = offset,
			},
			[1] = {
				.integer.type = ACPI_TYPE_INTEGER,
				.integer.value = len,
			},
			[2] = {
				.buffer.type = ACPI_TYPE_BUFFER,
				.buffer.pointer = data,
				.buffer.length = len,
			},
		},
	};

	rc = acpi_evaluate_object(handle, "_LSW", &input, &buf);
	if (ACPI_FAILURE(rc))
		return NULL;
	return int_to_buf(buf.pointer);
}

static union acpi_object *acpi_label_read(acpi_handle handle, u32 offset,
		u32 len)
{
	acpi_status rc;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input = {
		.count = 2,
		.pointer = (union acpi_object []) {
			[0] = {
				.integer.type = ACPI_TYPE_INTEGER,
				.integer.value = offset,
			},
			[1] = {
				.integer.type = ACPI_TYPE_INTEGER,
				.integer.value = len,
			},
		},
	};

	rc = acpi_evaluate_object(handle, "_LSR", &input, &buf);
	if (ACPI_FAILURE(rc))
		return NULL;
	return pkg_to_buf(buf.pointer);
}

static union acpi_object *acpi_label_info(acpi_handle handle)
{
	acpi_status rc;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };

	rc = acpi_evaluate_object(handle, "_LSI", NULL, &buf);
	if (ACPI_FAILURE(rc))
		return NULL;
	return pkg_to_buf(buf.pointer);
}

static u8 nfit_dsm_revid(unsigned family, unsigned func)
{
	static const u8 revid_table[NVDIMM_FAMILY_MAX+1][NVDIMM_CMD_MAX+1] = {
		[NVDIMM_FAMILY_INTEL] = {
			[NVDIMM_INTEL_GET_MODES ...
				NVDIMM_INTEL_FW_ACTIVATE_ARM] = 2,
		},
	};
	u8 id;

	if (family > NVDIMM_FAMILY_MAX)
		return 0;
	if (func > NVDIMM_CMD_MAX)
		return 0;
	id = revid_table[family][func];
	if (id == 0)
		return 1; /* default */
	return id;
}

static bool payload_dumpable(struct nvdimm *nvdimm, unsigned int func)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	if (nfit_mem && nfit_mem->family == NVDIMM_FAMILY_INTEL
			&& func >= NVDIMM_INTEL_GET_SECURITY_STATE
			&& func <= NVDIMM_INTEL_MASTER_SECURE_ERASE)
		return IS_ENABLED(CONFIG_NFIT_SECURITY_DEBUG);
	return true;
}

static int cmd_to_func(struct nfit_mem *nfit_mem, unsigned int cmd,
		struct nd_cmd_pkg *call_pkg, int *family)
{
	if (call_pkg) {
		int i;

		if (nfit_mem && nfit_mem->family != call_pkg->nd_family)
			return -ENOTTY;

		for (i = 0; i < ARRAY_SIZE(call_pkg->nd_reserved2); i++)
			if (call_pkg->nd_reserved2[i])
				return -EINVAL;
		*family = call_pkg->nd_family;
		return call_pkg->nd_command;
	}

	/* In the !call_pkg case, bus commands == bus functions */
	if (!nfit_mem)
		return cmd;

	/* Linux ND commands == NVDIMM_FAMILY_INTEL function numbers */
	if (nfit_mem->family == NVDIMM_FAMILY_INTEL)
		return cmd;

	/*
	 * Force function number validation to fail since 0 is never
	 * published as a valid function in dsm_mask.
	 */
	return 0;
}

int acpi_nfit_ctl(struct nvdimm_bus_descriptor *nd_desc, struct nvdimm *nvdimm,
		unsigned int cmd, void *buf, unsigned int buf_len, int *cmd_rc)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	union acpi_object in_obj, in_buf, *out_obj;
	const struct nd_cmd_desc *desc = NULL;
	struct device *dev = acpi_desc->dev;
	struct nd_cmd_pkg *call_pkg = NULL;
	const char *cmd_name, *dimm_name;
	unsigned long cmd_mask, dsm_mask;
	u32 offset, fw_status = 0;
	acpi_handle handle;
	const guid_t *guid;
	int func, rc, i;
	int family = 0;

	if (cmd_rc)
		*cmd_rc = -EINVAL;

	if (cmd == ND_CMD_CALL)
		call_pkg = buf;
	func = cmd_to_func(nfit_mem, cmd, call_pkg, &family);
	if (func < 0)
		return func;

	if (nvdimm) {
		struct acpi_device *adev = nfit_mem->adev;

		if (!adev)
			return -ENOTTY;

		dimm_name = nvdimm_name(nvdimm);
		cmd_name = nvdimm_cmd_name(cmd);
		cmd_mask = nvdimm_cmd_mask(nvdimm);
		dsm_mask = nfit_mem->dsm_mask;
		desc = nd_cmd_dimm_desc(cmd);
		guid = to_nfit_uuid(nfit_mem->family);
		handle = adev->handle;
	} else {
		struct acpi_device *adev = to_acpi_dev(acpi_desc);

		cmd_name = nvdimm_bus_cmd_name(cmd);
		cmd_mask = nd_desc->cmd_mask;
		if (cmd == ND_CMD_CALL && call_pkg->nd_family) {
			family = call_pkg->nd_family;
			if (family > NVDIMM_BUS_FAMILY_MAX ||
			    !test_bit(family, &nd_desc->bus_family_mask))
				return -EINVAL;
			family = array_index_nospec(family,
						    NVDIMM_BUS_FAMILY_MAX + 1);
			dsm_mask = acpi_desc->family_dsm_mask[family];
			guid = to_nfit_bus_uuid(family);
		} else {
			dsm_mask = acpi_desc->bus_dsm_mask;
			guid = to_nfit_uuid(NFIT_DEV_BUS);
		}
		desc = nd_cmd_bus_desc(cmd);
		handle = adev->handle;
		dimm_name = "bus";
	}

	if (!desc || (cmd && (desc->out_num + desc->in_num == 0)))
		return -ENOTTY;

	/*
	 * Check for a valid command.  For ND_CMD_CALL, we also have to
	 * make sure that the DSM function is supported.
	 */
	if (cmd == ND_CMD_CALL &&
	    (func > NVDIMM_CMD_MAX || !test_bit(func, &dsm_mask)))
		return -ENOTTY;
	else if (!test_bit(cmd, &cmd_mask))
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

	if (call_pkg) {
		/* skip over package wrapper */
		in_buf.buffer.pointer = (void *) &call_pkg->nd_payload;
		in_buf.buffer.length = call_pkg->nd_size_in;
	}

	dev_dbg(dev, "%s cmd: %d: family: %d func: %d input length: %d\n",
		dimm_name, cmd, family, func, in_buf.buffer.length);
	if (payload_dumpable(nvdimm, func))
		print_hex_dump_debug("nvdimm in  ", DUMP_PREFIX_OFFSET, 4, 4,
				in_buf.buffer.pointer,
				min_t(u32, 256, in_buf.buffer.length), true);

	/* call the BIOS, prefer the named methods over _DSM if available */
	if (nvdimm && cmd == ND_CMD_GET_CONFIG_SIZE
			&& test_bit(NFIT_MEM_LSR, &nfit_mem->flags))
		out_obj = acpi_label_info(handle);
	else if (nvdimm && cmd == ND_CMD_GET_CONFIG_DATA
			&& test_bit(NFIT_MEM_LSR, &nfit_mem->flags)) {
		struct nd_cmd_get_config_data_hdr *p = buf;

		out_obj = acpi_label_read(handle, p->in_offset, p->in_length);
	} else if (nvdimm && cmd == ND_CMD_SET_CONFIG_DATA
			&& test_bit(NFIT_MEM_LSW, &nfit_mem->flags)) {
		struct nd_cmd_set_config_hdr *p = buf;

		out_obj = acpi_label_write(handle, p->in_offset, p->in_length,
				p->in_buf);
	} else {
		u8 revid;

		if (nvdimm)
			revid = nfit_dsm_revid(nfit_mem->family, func);
		else
			revid = 1;
		out_obj = acpi_evaluate_dsm(handle, guid, revid, func, &in_obj);
	}

	if (!out_obj) {
		dev_dbg(dev, "%s _DSM failed cmd: %s\n", dimm_name, cmd_name);
		return -EINVAL;
	}

	if (out_obj->type != ACPI_TYPE_BUFFER) {
		dev_dbg(dev, "%s unexpected output object type cmd: %s type: %d\n",
				dimm_name, cmd_name, out_obj->type);
		rc = -EINVAL;
		goto out;
	}

	dev_dbg(dev, "%s cmd: %s output length: %d\n", dimm_name,
			cmd_name, out_obj->buffer.length);
	print_hex_dump_debug(cmd_name, DUMP_PREFIX_OFFSET, 4, 4,
			out_obj->buffer.pointer,
			min_t(u32, 128, out_obj->buffer.length), true);

	if (call_pkg) {
		call_pkg->nd_fw_size = out_obj->buffer.length;
		memcpy(call_pkg->nd_payload + call_pkg->nd_size_in,
			out_obj->buffer.pointer,
			min(call_pkg->nd_fw_size, call_pkg->nd_size_out));

		ACPI_FREE(out_obj);
		/*
		 * Need to support FW function w/o known size in advance.
		 * Caller can determine required size based upon nd_fw_size.
		 * If we return an error (like elsewhere) then caller wouldn't
		 * be able to rely upon data returned to make calculation.
		 */
		if (cmd_rc)
			*cmd_rc = 0;
		return 0;
	}

	for (i = 0, offset = 0; i < desc->out_num; i++) {
		u32 out_size = nd_cmd_out_size(nvdimm, cmd, desc, i, buf,
				(u32 *) out_obj->buffer.pointer,
				out_obj->buffer.length - offset);

		if (offset + out_size > out_obj->buffer.length) {
			dev_dbg(dev, "%s output object underflow cmd: %s field: %d\n",
					dimm_name, cmd_name, i);
			break;
		}

		if (in_buf.buffer.length + offset + out_size > buf_len) {
			dev_dbg(dev, "%s output overrun cmd: %s field: %d\n",
					dimm_name, cmd_name, i);
			rc = -ENXIO;
			goto out;
		}
		memcpy(buf + in_buf.buffer.length + offset,
				out_obj->buffer.pointer + offset, out_size);
		offset += out_size;
	}

	/*
	 * Set fw_status for all the commands with a known format to be
	 * later interpreted by xlat_status().
	 */
	if (i >= 1 && ((!nvdimm && cmd >= ND_CMD_ARS_CAP
					&& cmd <= ND_CMD_CLEAR_ERROR)
				|| (nvdimm && cmd >= ND_CMD_SMART
					&& cmd <= ND_CMD_VENDOR)))
		fw_status = *(u32 *) out_obj->buffer.pointer;

	if (offset + in_buf.buffer.length < buf_len) {
		if (i >= 1) {
			/*
			 * status valid, return the number of bytes left
			 * unfilled in the output buffer
			 */
			rc = buf_len - offset - in_buf.buffer.length;
			if (cmd_rc)
				*cmd_rc = xlat_status(nvdimm, buf, cmd,
						fw_status);
		} else {
			dev_err(dev, "%s:%s underrun cmd: %s buf_len: %d out_len: %d\n",
					__func__, dimm_name, cmd_name, buf_len,
					offset);
			rc = -ENXIO;
		}
	} else {
		rc = 0;
		if (cmd_rc)
			*cmd_rc = xlat_status(nvdimm, buf, cmd, fw_status);
	}

 out:
	ACPI_FREE(out_obj);

	return rc;
}
EXPORT_SYMBOL_GPL(acpi_nfit_ctl);

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

int nfit_spa_type(struct acpi_nfit_system_address *spa)
{
	int i;

	for (i = 0; i < NFIT_UUID_MAX; i++)
		if (guid_equal(to_nfit_uuid(i), (guid_t *)&spa->range_guid))
			return i;
	return -1;
}

static size_t sizeof_spa(struct acpi_nfit_system_address *spa)
{
	if (spa->flags & ACPI_NFIT_LOCATION_COOKIE_VALID)
		return sizeof(*spa);
	return sizeof(*spa) - 8;
}

static bool add_spa(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev,
		struct acpi_nfit_system_address *spa)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_spa *nfit_spa;

	if (spa->header.length != sizeof_spa(spa))
		return false;

	list_for_each_entry(nfit_spa, &prev->spas, list) {
		if (memcmp(nfit_spa->spa, spa, sizeof_spa(spa)) == 0) {
			list_move_tail(&nfit_spa->list, &acpi_desc->spas);
			return true;
		}
	}

	nfit_spa = devm_kzalloc(dev, sizeof(*nfit_spa) + sizeof_spa(spa),
			GFP_KERNEL);
	if (!nfit_spa)
		return false;
	INIT_LIST_HEAD(&nfit_spa->list);
	memcpy(nfit_spa->spa, spa, sizeof_spa(spa));
	list_add_tail(&nfit_spa->list, &acpi_desc->spas);
	dev_dbg(dev, "spa index: %d type: %s\n",
			spa->range_index,
			spa_type_name(nfit_spa_type(spa)));
	return true;
}

static bool add_memdev(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev,
		struct acpi_nfit_memory_map *memdev)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_memdev *nfit_memdev;

	if (memdev->header.length != sizeof(*memdev))
		return false;

	list_for_each_entry(nfit_memdev, &prev->memdevs, list)
		if (memcmp(nfit_memdev->memdev, memdev, sizeof(*memdev)) == 0) {
			list_move_tail(&nfit_memdev->list, &acpi_desc->memdevs);
			return true;
		}

	nfit_memdev = devm_kzalloc(dev, sizeof(*nfit_memdev) + sizeof(*memdev),
			GFP_KERNEL);
	if (!nfit_memdev)
		return false;
	INIT_LIST_HEAD(&nfit_memdev->list);
	memcpy(nfit_memdev->memdev, memdev, sizeof(*memdev));
	list_add_tail(&nfit_memdev->list, &acpi_desc->memdevs);
	dev_dbg(dev, "memdev handle: %#x spa: %d dcr: %d flags: %#x\n",
			memdev->device_handle, memdev->range_index,
			memdev->region_index, memdev->flags);
	return true;
}

int nfit_get_smbios_id(u32 device_handle, u16 *flags)
{
	struct acpi_nfit_memory_map *memdev;
	struct acpi_nfit_desc *acpi_desc;
	struct nfit_mem *nfit_mem;
	u16 physical_id;

	mutex_lock(&acpi_desc_lock);
	list_for_each_entry(acpi_desc, &acpi_descs, list) {
		mutex_lock(&acpi_desc->init_mutex);
		list_for_each_entry(nfit_mem, &acpi_desc->dimms, list) {
			memdev = __to_nfit_memdev(nfit_mem);
			if (memdev->device_handle == device_handle) {
				*flags = memdev->flags;
				physical_id = memdev->physical_id;
				mutex_unlock(&acpi_desc->init_mutex);
				mutex_unlock(&acpi_desc_lock);
				return physical_id;
			}
		}
		mutex_unlock(&acpi_desc->init_mutex);
	}
	mutex_unlock(&acpi_desc_lock);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(nfit_get_smbios_id);

/*
 * An implementation may provide a truncated control region if no block windows
 * are defined.
 */
static size_t sizeof_dcr(struct acpi_nfit_control_region *dcr)
{
	if (dcr->header.length < offsetof(struct acpi_nfit_control_region,
				window_size))
		return 0;
	if (dcr->windows)
		return sizeof(*dcr);
	return offsetof(struct acpi_nfit_control_region, window_size);
}

static bool add_dcr(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev,
		struct acpi_nfit_control_region *dcr)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_dcr *nfit_dcr;

	if (!sizeof_dcr(dcr))
		return false;

	list_for_each_entry(nfit_dcr, &prev->dcrs, list)
		if (memcmp(nfit_dcr->dcr, dcr, sizeof_dcr(dcr)) == 0) {
			list_move_tail(&nfit_dcr->list, &acpi_desc->dcrs);
			return true;
		}

	nfit_dcr = devm_kzalloc(dev, sizeof(*nfit_dcr) + sizeof(*dcr),
			GFP_KERNEL);
	if (!nfit_dcr)
		return false;
	INIT_LIST_HEAD(&nfit_dcr->list);
	memcpy(nfit_dcr->dcr, dcr, sizeof_dcr(dcr));
	list_add_tail(&nfit_dcr->list, &acpi_desc->dcrs);
	dev_dbg(dev, "dcr index: %d windows: %d\n",
			dcr->region_index, dcr->windows);
	return true;
}

static bool add_bdw(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev,
		struct acpi_nfit_data_region *bdw)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_bdw *nfit_bdw;

	if (bdw->header.length != sizeof(*bdw))
		return false;
	list_for_each_entry(nfit_bdw, &prev->bdws, list)
		if (memcmp(nfit_bdw->bdw, bdw, sizeof(*bdw)) == 0) {
			list_move_tail(&nfit_bdw->list, &acpi_desc->bdws);
			return true;
		}

	nfit_bdw = devm_kzalloc(dev, sizeof(*nfit_bdw) + sizeof(*bdw),
			GFP_KERNEL);
	if (!nfit_bdw)
		return false;
	INIT_LIST_HEAD(&nfit_bdw->list);
	memcpy(nfit_bdw->bdw, bdw, sizeof(*bdw));
	list_add_tail(&nfit_bdw->list, &acpi_desc->bdws);
	dev_dbg(dev, "bdw dcr: %d windows: %d\n",
			bdw->region_index, bdw->windows);
	return true;
}

static size_t sizeof_idt(struct acpi_nfit_interleave *idt)
{
	if (idt->header.length < sizeof(*idt))
		return 0;
	return sizeof(*idt) + sizeof(u32) * (idt->line_count - 1);
}

static bool add_idt(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev,
		struct acpi_nfit_interleave *idt)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_idt *nfit_idt;

	if (!sizeof_idt(idt))
		return false;

	list_for_each_entry(nfit_idt, &prev->idts, list) {
		if (sizeof_idt(nfit_idt->idt) != sizeof_idt(idt))
			continue;

		if (memcmp(nfit_idt->idt, idt, sizeof_idt(idt)) == 0) {
			list_move_tail(&nfit_idt->list, &acpi_desc->idts);
			return true;
		}
	}

	nfit_idt = devm_kzalloc(dev, sizeof(*nfit_idt) + sizeof_idt(idt),
			GFP_KERNEL);
	if (!nfit_idt)
		return false;
	INIT_LIST_HEAD(&nfit_idt->list);
	memcpy(nfit_idt->idt, idt, sizeof_idt(idt));
	list_add_tail(&nfit_idt->list, &acpi_desc->idts);
	dev_dbg(dev, "idt index: %d num_lines: %d\n",
			idt->interleave_index, idt->line_count);
	return true;
}

static size_t sizeof_flush(struct acpi_nfit_flush_address *flush)
{
	if (flush->header.length < sizeof(*flush))
		return 0;
	return sizeof(*flush) + sizeof(u64) * (flush->hint_count - 1);
}

static bool add_flush(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev,
		struct acpi_nfit_flush_address *flush)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_flush *nfit_flush;

	if (!sizeof_flush(flush))
		return false;

	list_for_each_entry(nfit_flush, &prev->flushes, list) {
		if (sizeof_flush(nfit_flush->flush) != sizeof_flush(flush))
			continue;

		if (memcmp(nfit_flush->flush, flush,
					sizeof_flush(flush)) == 0) {
			list_move_tail(&nfit_flush->list, &acpi_desc->flushes);
			return true;
		}
	}

	nfit_flush = devm_kzalloc(dev, sizeof(*nfit_flush)
			+ sizeof_flush(flush), GFP_KERNEL);
	if (!nfit_flush)
		return false;
	INIT_LIST_HEAD(&nfit_flush->list);
	memcpy(nfit_flush->flush, flush, sizeof_flush(flush));
	list_add_tail(&nfit_flush->list, &acpi_desc->flushes);
	dev_dbg(dev, "nfit_flush handle: %d hint_count: %d\n",
			flush->device_handle, flush->hint_count);
	return true;
}

static bool add_platform_cap(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_capabilities *pcap)
{
	struct device *dev = acpi_desc->dev;
	u32 mask;

	mask = (1 << (pcap->highest_capability + 1)) - 1;
	acpi_desc->platform_cap = pcap->capabilities & mask;
	dev_dbg(dev, "cap: %#x\n", acpi_desc->platform_cap);
	return true;
}

static void *add_table(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev, void *table, const void *end)
{
	struct device *dev = acpi_desc->dev;
	struct acpi_nfit_header *hdr;
	void *err = ERR_PTR(-ENOMEM);

	if (table >= end)
		return NULL;

	hdr = table;
	if (!hdr->length) {
		dev_warn(dev, "found a zero length table '%d' parsing nfit\n",
			hdr->type);
		return NULL;
	}

	switch (hdr->type) {
	case ACPI_NFIT_TYPE_SYSTEM_ADDRESS:
		if (!add_spa(acpi_desc, prev, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_MEMORY_MAP:
		if (!add_memdev(acpi_desc, prev, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_CONTROL_REGION:
		if (!add_dcr(acpi_desc, prev, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_DATA_REGION:
		if (!add_bdw(acpi_desc, prev, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_INTERLEAVE:
		if (!add_idt(acpi_desc, prev, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_FLUSH_ADDRESS:
		if (!add_flush(acpi_desc, prev, table))
			return err;
		break;
	case ACPI_NFIT_TYPE_SMBIOS:
		dev_dbg(dev, "smbios\n");
		break;
	case ACPI_NFIT_TYPE_CAPABILITIES:
		if (!add_platform_cap(acpi_desc, table))
			return err;
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

static void nfit_mem_init_bdw(struct acpi_nfit_desc *acpi_desc,
		struct nfit_mem *nfit_mem, struct acpi_nfit_system_address *spa)
{
	u16 dcr = __to_nfit_memdev(nfit_mem)->region_index;
	struct nfit_memdev *nfit_memdev;
	struct nfit_bdw *nfit_bdw;
	struct nfit_idt *nfit_idt;
	u16 idt_idx, range_index;

	list_for_each_entry(nfit_bdw, &acpi_desc->bdws, list) {
		if (nfit_bdw->bdw->region_index != dcr)
			continue;
		nfit_mem->bdw = nfit_bdw->bdw;
		break;
	}

	if (!nfit_mem->bdw)
		return;

	nfit_mem_find_spa_bdw(acpi_desc, nfit_mem);

	if (!nfit_mem->spa_bdw)
		return;

	range_index = nfit_mem->spa_bdw->range_index;
	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
		if (nfit_memdev->memdev->range_index != range_index ||
				nfit_memdev->memdev->region_index != dcr)
			continue;
		nfit_mem->memdev_bdw = nfit_memdev->memdev;
		idt_idx = nfit_memdev->memdev->interleave_index;
		list_for_each_entry(nfit_idt, &acpi_desc->idts, list) {
			if (nfit_idt->idt->interleave_index != idt_idx)
				continue;
			nfit_mem->idt_bdw = nfit_idt->idt;
			break;
		}
		break;
	}
}

static int __nfit_mem_init(struct acpi_nfit_desc *acpi_desc,
		struct acpi_nfit_system_address *spa)
{
	struct nfit_mem *nfit_mem, *found;
	struct nfit_memdev *nfit_memdev;
	int type = spa ? nfit_spa_type(spa) : 0;

	switch (type) {
	case NFIT_SPA_DCR:
	case NFIT_SPA_PM:
		break;
	default:
		if (spa)
			return 0;
	}

	/*
	 * This loop runs in two modes, when a dimm is mapped the loop
	 * adds memdev associations to an existing dimm, or creates a
	 * dimm. In the unmapped dimm case this loop sweeps for memdev
	 * instances with an invalid / zero range_index and adds those
	 * dimms without spa associations.
	 */
	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
		struct nfit_flush *nfit_flush;
		struct nfit_dcr *nfit_dcr;
		u32 device_handle;
		u16 dcr;

		if (spa && nfit_memdev->memdev->range_index != spa->range_index)
			continue;
		if (!spa && nfit_memdev->memdev->range_index)
			continue;
		found = NULL;
		dcr = nfit_memdev->memdev->region_index;
		device_handle = nfit_memdev->memdev->device_handle;
		list_for_each_entry(nfit_mem, &acpi_desc->dimms, list)
			if (__to_nfit_memdev(nfit_mem)->device_handle
					== device_handle) {
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
			nfit_mem->acpi_desc = acpi_desc;
			list_add(&nfit_mem->list, &acpi_desc->dimms);
		}

		list_for_each_entry(nfit_dcr, &acpi_desc->dcrs, list) {
			if (nfit_dcr->dcr->region_index != dcr)
				continue;
			/*
			 * Record the control region for the dimm.  For
			 * the ACPI 6.1 case, where there are separate
			 * control regions for the pmem vs blk
			 * interfaces, be sure to record the extended
			 * blk details.
			 */
			if (!nfit_mem->dcr)
				nfit_mem->dcr = nfit_dcr->dcr;
			else if (nfit_mem->dcr->windows == 0
					&& nfit_dcr->dcr->windows)
				nfit_mem->dcr = nfit_dcr->dcr;
			break;
		}

		list_for_each_entry(nfit_flush, &acpi_desc->flushes, list) {
			struct acpi_nfit_flush_address *flush;
			u16 i;

			if (nfit_flush->flush->device_handle != device_handle)
				continue;
			nfit_mem->nfit_flush = nfit_flush;
			flush = nfit_flush->flush;
			nfit_mem->flush_wpq = devm_kcalloc(acpi_desc->dev,
					flush->hint_count,
					sizeof(struct resource),
					GFP_KERNEL);
			if (!nfit_mem->flush_wpq)
				return -ENOMEM;
			for (i = 0; i < flush->hint_count; i++) {
				struct resource *res = &nfit_mem->flush_wpq[i];

				res->start = flush->hint_address[i];
				res->end = res->start + 8 - 1;
			}
			break;
		}

		if (dcr && !nfit_mem->dcr) {
			dev_err(acpi_desc->dev, "SPA %d missing DCR %d\n",
					spa->range_index, dcr);
			return -ENODEV;
		}

		if (type == NFIT_SPA_DCR) {
			struct nfit_idt *nfit_idt;
			u16 idt_idx;

			/* multiple dimms may share a SPA when interleaved */
			nfit_mem->spa_dcr = spa;
			nfit_mem->memdev_dcr = nfit_memdev->memdev;
			idt_idx = nfit_memdev->memdev->interleave_index;
			list_for_each_entry(nfit_idt, &acpi_desc->idts, list) {
				if (nfit_idt->idt->interleave_index != idt_idx)
					continue;
				nfit_mem->idt_dcr = nfit_idt->idt;
				break;
			}
			nfit_mem_init_bdw(acpi_desc, nfit_mem, spa);
		} else if (type == NFIT_SPA_PM) {
			/*
			 * A single dimm may belong to multiple SPA-PM
			 * ranges, record at least one in addition to
			 * any SPA-DCR range.
			 */
			nfit_mem->memdev_pmem = nfit_memdev->memdev;
		} else
			nfit_mem->memdev_dcr = nfit_memdev->memdev;
	}

	return 0;
}

static int nfit_mem_cmp(void *priv, const struct list_head *_a,
		const struct list_head *_b)
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
	int rc;


	/*
	 * For each SPA-DCR or SPA-PMEM address range find its
	 * corresponding MEMDEV(s).  From each MEMDEV find the
	 * corresponding DCR.  Then, if we're operating on a SPA-DCR,
	 * try to find a SPA-BDW and a corresponding BDW that references
	 * the DCR.  Throw it all into an nfit_mem object.  Note, that
	 * BDWs are optional.
	 */
	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		rc = __nfit_mem_init(acpi_desc, nfit_spa->spa);
		if (rc)
			return rc;
	}

	/*
	 * If a DIMM has failed to be mapped into SPA there will be no
	 * SPA entries above. Find and register all the unmapped DIMMs
	 * for reporting and recovery purposes.
	 */
	rc = __nfit_mem_init(acpi_desc, NULL);
	if (rc)
		return rc;

	list_sort(NULL, &acpi_desc->dimms, nfit_mem_cmp);

	return 0;
}

static ssize_t bus_dsm_mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	return sprintf(buf, "%#lx\n", acpi_desc->bus_dsm_mask);
}
static struct device_attribute dev_attr_bus_dsm_mask =
		__ATTR(dsm_mask, 0444, bus_dsm_mask_show, NULL);

static ssize_t revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	return sprintf(buf, "%d\n", acpi_desc->acpi_header.revision);
}
static DEVICE_ATTR_RO(revision);

static ssize_t hw_error_scrub_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	return sprintf(buf, "%d\n", acpi_desc->scrub_mode);
}

/*
 * The 'hw_error_scrub' attribute can have the following values written to it:
 * '0': Switch to the default mode where an exception will only insert
 *      the address of the memory error into the poison and badblocks lists.
 * '1': Enable a full scrub to happen if an exception for a memory error is
 *      received.
 */
static ssize_t hw_error_scrub_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct nvdimm_bus_descriptor *nd_desc;
	ssize_t rc;
	long val;

	rc = kstrtol(buf, 0, &val);
	if (rc)
		return rc;

	nfit_device_lock(dev);
	nd_desc = dev_get_drvdata(dev);
	if (nd_desc) {
		struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

		switch (val) {
		case HW_ERROR_SCRUB_ON:
			acpi_desc->scrub_mode = HW_ERROR_SCRUB_ON;
			break;
		case HW_ERROR_SCRUB_OFF:
			acpi_desc->scrub_mode = HW_ERROR_SCRUB_OFF;
			break;
		default:
			rc = -EINVAL;
			break;
		}
	}
	nfit_device_unlock(dev);
	if (rc)
		return rc;
	return size;
}
static DEVICE_ATTR_RW(hw_error_scrub);

/*
 * This shows the number of full Address Range Scrubs that have been
 * completed since driver load time. Userspace can wait on this using
 * select/poll etc. A '+' at the end indicates an ARS is in progress
 */
static ssize_t scrub_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus_descriptor *nd_desc;
	struct acpi_nfit_desc *acpi_desc;
	ssize_t rc = -ENXIO;
	bool busy;

	nfit_device_lock(dev);
	nd_desc = dev_get_drvdata(dev);
	if (!nd_desc) {
		nfit_device_unlock(dev);
		return rc;
	}
	acpi_desc = to_acpi_desc(nd_desc);

	mutex_lock(&acpi_desc->init_mutex);
	busy = test_bit(ARS_BUSY, &acpi_desc->scrub_flags)
		&& !test_bit(ARS_CANCEL, &acpi_desc->scrub_flags);
	rc = sprintf(buf, "%d%s", acpi_desc->scrub_count, busy ? "+\n" : "\n");
	/* Allow an admin to poll the busy state at a higher rate */
	if (busy && capable(CAP_SYS_RAWIO) && !test_and_set_bit(ARS_POLL,
				&acpi_desc->scrub_flags)) {
		acpi_desc->scrub_tmo = 1;
		mod_delayed_work(nfit_wq, &acpi_desc->dwork, HZ);
	}

	mutex_unlock(&acpi_desc->init_mutex);
	nfit_device_unlock(dev);
	return rc;
}

static ssize_t scrub_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct nvdimm_bus_descriptor *nd_desc;
	ssize_t rc;
	long val;

	rc = kstrtol(buf, 0, &val);
	if (rc)
		return rc;
	if (val != 1)
		return -EINVAL;

	nfit_device_lock(dev);
	nd_desc = dev_get_drvdata(dev);
	if (nd_desc) {
		struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

		rc = acpi_nfit_ars_rescan(acpi_desc, ARS_REQ_LONG);
	}
	nfit_device_unlock(dev);
	if (rc)
		return rc;
	return size;
}
static DEVICE_ATTR_RW(scrub);

static bool ars_supported(struct nvdimm_bus *nvdimm_bus)
{
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	const unsigned long mask = 1 << ND_CMD_ARS_CAP | 1 << ND_CMD_ARS_START
		| 1 << ND_CMD_ARS_STATUS;

	return (nd_desc->cmd_mask & mask) == mask;
}

static umode_t nfit_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);

	if (a == &dev_attr_scrub.attr)
		return ars_supported(nvdimm_bus) ? a->mode : 0;

	if (a == &dev_attr_firmware_activate_noidle.attr)
		return intel_fwa_supported(nvdimm_bus) ? a->mode : 0;

	return a->mode;
}

static struct attribute *acpi_nfit_attributes[] = {
	&dev_attr_revision.attr,
	&dev_attr_scrub.attr,
	&dev_attr_hw_error_scrub.attr,
	&dev_attr_bus_dsm_mask.attr,
	&dev_attr_firmware_activate_noidle.attr,
	NULL,
};

static const struct attribute_group acpi_nfit_attribute_group = {
	.name = "nfit",
	.attrs = acpi_nfit_attributes,
	.is_visible = nfit_visible,
};

static const struct attribute_group *acpi_nfit_attribute_groups[] = {
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

	return sprintf(buf, "0x%04x\n", be16_to_cpu(dcr->vendor_id));
}
static DEVICE_ATTR_RO(vendor);

static ssize_t rev_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%04x\n", be16_to_cpu(dcr->revision_id));
}
static DEVICE_ATTR_RO(rev_id);

static ssize_t device_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%04x\n", be16_to_cpu(dcr->device_id));
}
static DEVICE_ATTR_RO(device);

static ssize_t subsystem_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%04x\n", be16_to_cpu(dcr->subsystem_vendor_id));
}
static DEVICE_ATTR_RO(subsystem_vendor);

static ssize_t subsystem_rev_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%04x\n",
			be16_to_cpu(dcr->subsystem_revision_id));
}
static DEVICE_ATTR_RO(subsystem_rev_id);

static ssize_t subsystem_device_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%04x\n", be16_to_cpu(dcr->subsystem_device_id));
}
static DEVICE_ATTR_RO(subsystem_device);

static int num_nvdimm_formats(struct nvdimm *nvdimm)
{
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	int formats = 0;

	if (nfit_mem->memdev_pmem)
		formats++;
	if (nfit_mem->memdev_bdw)
		formats++;
	return formats;
}

static ssize_t format_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%04x\n", le16_to_cpu(dcr->code));
}
static DEVICE_ATTR_RO(format);

static ssize_t format1_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 handle;
	ssize_t rc = -ENXIO;
	struct nfit_mem *nfit_mem;
	struct nfit_memdev *nfit_memdev;
	struct acpi_nfit_desc *acpi_desc;
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	nfit_mem = nvdimm_provider_data(nvdimm);
	acpi_desc = nfit_mem->acpi_desc;
	handle = to_nfit_memdev(dev)->device_handle;

	/* assumes DIMMs have at most 2 published interface codes */
	mutex_lock(&acpi_desc->init_mutex);
	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
		struct acpi_nfit_memory_map *memdev = nfit_memdev->memdev;
		struct nfit_dcr *nfit_dcr;

		if (memdev->device_handle != handle)
			continue;

		list_for_each_entry(nfit_dcr, &acpi_desc->dcrs, list) {
			if (nfit_dcr->dcr->region_index != memdev->region_index)
				continue;
			if (nfit_dcr->dcr->code == dcr->code)
				continue;
			rc = sprintf(buf, "0x%04x\n",
					le16_to_cpu(nfit_dcr->dcr->code));
			break;
		}
		if (rc != -ENXIO)
			break;
	}
	mutex_unlock(&acpi_desc->init_mutex);
	return rc;
}
static DEVICE_ATTR_RO(format1);

static ssize_t formats_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);

	return sprintf(buf, "%d\n", num_nvdimm_formats(nvdimm));
}
static DEVICE_ATTR_RO(formats);

static ssize_t serial_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct acpi_nfit_control_region *dcr = to_nfit_dcr(dev);

	return sprintf(buf, "0x%08x\n", be32_to_cpu(dcr->serial_number));
}
static DEVICE_ATTR_RO(serial);

static ssize_t family_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	if (nfit_mem->family < 0)
		return -ENXIO;
	return sprintf(buf, "%d\n", nfit_mem->family);
}
static DEVICE_ATTR_RO(family);

static ssize_t dsm_mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	if (nfit_mem->family < 0)
		return -ENXIO;
	return sprintf(buf, "%#lx\n", nfit_mem->dsm_mask);
}
static DEVICE_ATTR_RO(dsm_mask);

static ssize_t flags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
	u16 flags = __to_nfit_memdev(nfit_mem)->flags;

	if (test_bit(NFIT_MEM_DIRTY, &nfit_mem->flags))
		flags |= ACPI_NFIT_MEM_FLUSH_FAILED;

	return sprintf(buf, "%s%s%s%s%s%s%s\n",
		flags & ACPI_NFIT_MEM_SAVE_FAILED ? "save_fail " : "",
		flags & ACPI_NFIT_MEM_RESTORE_FAILED ? "restore_fail " : "",
		flags & ACPI_NFIT_MEM_FLUSH_FAILED ? "flush_fail " : "",
		flags & ACPI_NFIT_MEM_NOT_ARMED ? "not_armed " : "",
		flags & ACPI_NFIT_MEM_HEALTH_OBSERVED ? "smart_event " : "",
		flags & ACPI_NFIT_MEM_MAP_FAILED ? "map_fail " : "",
		flags & ACPI_NFIT_MEM_HEALTH_ENABLED ? "smart_notify " : "");
}
static DEVICE_ATTR_RO(flags);

static ssize_t id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	return sprintf(buf, "%s\n", nfit_mem->id);
}
static DEVICE_ATTR_RO(id);

static ssize_t dirty_shutdown_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	return sprintf(buf, "%d\n", nfit_mem->dirty_shutdown);
}
static DEVICE_ATTR_RO(dirty_shutdown);

static struct attribute *acpi_nfit_dimm_attributes[] = {
	&dev_attr_handle.attr,
	&dev_attr_phys_id.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_rev_id.attr,
	&dev_attr_subsystem_vendor.attr,
	&dev_attr_subsystem_device.attr,
	&dev_attr_subsystem_rev_id.attr,
	&dev_attr_format.attr,
	&dev_attr_formats.attr,
	&dev_attr_format1.attr,
	&dev_attr_serial.attr,
	&dev_attr_flags.attr,
	&dev_attr_id.attr,
	&dev_attr_family.attr,
	&dev_attr_dsm_mask.attr,
	&dev_attr_dirty_shutdown.attr,
	NULL,
};

static umode_t acpi_nfit_dimm_attr_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);

	if (!to_nfit_dcr(dev)) {
		/* Without a dcr only the memdev attributes can be surfaced */
		if (a == &dev_attr_handle.attr || a == &dev_attr_phys_id.attr
				|| a == &dev_attr_flags.attr
				|| a == &dev_attr_family.attr
				|| a == &dev_attr_dsm_mask.attr)
			return a->mode;
		return 0;
	}

	if (a == &dev_attr_format1.attr && num_nvdimm_formats(nvdimm) <= 1)
		return 0;

	if (!test_bit(NFIT_MEM_DIRTY_COUNT, &nfit_mem->flags)
			&& a == &dev_attr_dirty_shutdown.attr)
		return 0;

	return a->mode;
}

static const struct attribute_group acpi_nfit_dimm_attribute_group = {
	.name = "nfit",
	.attrs = acpi_nfit_dimm_attributes,
	.is_visible = acpi_nfit_dimm_attr_visible,
};

static const struct attribute_group *acpi_nfit_dimm_attribute_groups[] = {
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

void __acpi_nvdimm_notify(struct device *dev, u32 event)
{
	struct nfit_mem *nfit_mem;
	struct acpi_nfit_desc *acpi_desc;

	dev_dbg(dev->parent, "%s: event: %d\n", dev_name(dev),
			event);

	if (event != NFIT_NOTIFY_DIMM_HEALTH) {
		dev_dbg(dev->parent, "%s: unknown event: %d\n", dev_name(dev),
				event);
		return;
	}

	acpi_desc = dev_get_drvdata(dev->parent);
	if (!acpi_desc)
		return;

	/*
	 * If we successfully retrieved acpi_desc, then we know nfit_mem data
	 * is still valid.
	 */
	nfit_mem = dev_get_drvdata(dev);
	if (nfit_mem && nfit_mem->flags_attr)
		sysfs_notify_dirent(nfit_mem->flags_attr);
}
EXPORT_SYMBOL_GPL(__acpi_nvdimm_notify);

static void acpi_nvdimm_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *adev = data;
	struct device *dev = &adev->dev;

	nfit_device_lock(dev->parent);
	__acpi_nvdimm_notify(dev, event);
	nfit_device_unlock(dev->parent);
}

static bool acpi_nvdimm_has_method(struct acpi_device *adev, char *method)
{
	acpi_handle handle;
	acpi_status status;

	status = acpi_get_handle(adev->handle, method, &handle);

	if (ACPI_SUCCESS(status))
		return true;
	return false;
}

__weak void nfit_intel_shutdown_status(struct nfit_mem *nfit_mem)
{
	struct device *dev = &nfit_mem->adev->dev;
	struct nd_intel_smart smart = { 0 };
	union acpi_object in_buf = {
		.buffer.type = ACPI_TYPE_BUFFER,
		.buffer.length = 0,
	};
	union acpi_object in_obj = {
		.package.type = ACPI_TYPE_PACKAGE,
		.package.count = 1,
		.package.elements = &in_buf,
	};
	const u8 func = ND_INTEL_SMART;
	const guid_t *guid = to_nfit_uuid(nfit_mem->family);
	u8 revid = nfit_dsm_revid(nfit_mem->family, func);
	struct acpi_device *adev = nfit_mem->adev;
	acpi_handle handle = adev->handle;
	union acpi_object *out_obj;

	if ((nfit_mem->dsm_mask & (1 << func)) == 0)
		return;

	out_obj = acpi_evaluate_dsm(handle, guid, revid, func, &in_obj);
	if (!out_obj || out_obj->type != ACPI_TYPE_BUFFER
			|| out_obj->buffer.length < sizeof(smart)) {
		dev_dbg(dev->parent, "%s: failed to retrieve initial health\n",
				dev_name(dev));
		ACPI_FREE(out_obj);
		return;
	}
	memcpy(&smart, out_obj->buffer.pointer, sizeof(smart));
	ACPI_FREE(out_obj);

	if (smart.flags & ND_INTEL_SMART_SHUTDOWN_VALID) {
		if (smart.shutdown_state)
			set_bit(NFIT_MEM_DIRTY, &nfit_mem->flags);
	}

	if (smart.flags & ND_INTEL_SMART_SHUTDOWN_COUNT_VALID) {
		set_bit(NFIT_MEM_DIRTY_COUNT, &nfit_mem->flags);
		nfit_mem->dirty_shutdown = smart.shutdown_count;
	}
}

static void populate_shutdown_status(struct nfit_mem *nfit_mem)
{
	/*
	 * For DIMMs that provide a dynamic facility to retrieve a
	 * dirty-shutdown status and/or a dirty-shutdown count, cache
	 * these values in nfit_mem.
	 */
	if (nfit_mem->family == NVDIMM_FAMILY_INTEL)
		nfit_intel_shutdown_status(nfit_mem);
}

static int acpi_nfit_add_dimm(struct acpi_nfit_desc *acpi_desc,
		struct nfit_mem *nfit_mem, u32 device_handle)
{
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;
	struct acpi_device *adev, *adev_dimm;
	struct device *dev = acpi_desc->dev;
	unsigned long dsm_mask, label_mask;
	const guid_t *guid;
	int i;
	int family = -1;
	struct acpi_nfit_control_region *dcr = nfit_mem->dcr;

	/* nfit test assumes 1:1 relationship between commands and dsms */
	nfit_mem->dsm_mask = acpi_desc->dimm_cmd_force_en;
	nfit_mem->family = NVDIMM_FAMILY_INTEL;
	set_bit(NVDIMM_FAMILY_INTEL, &nd_desc->dimm_family_mask);

	if (dcr->valid_fields & ACPI_NFIT_CONTROL_MFG_INFO_VALID)
		sprintf(nfit_mem->id, "%04x-%02x-%04x-%08x",
				be16_to_cpu(dcr->vendor_id),
				dcr->manufacturing_location,
				be16_to_cpu(dcr->manufacturing_date),
				be32_to_cpu(dcr->serial_number));
	else
		sprintf(nfit_mem->id, "%04x-%08x",
				be16_to_cpu(dcr->vendor_id),
				be32_to_cpu(dcr->serial_number));

	adev = to_acpi_dev(acpi_desc);
	if (!adev) {
		/* unit test case */
		populate_shutdown_status(nfit_mem);
		return 0;
	}

	adev_dimm = acpi_find_child_device(adev, device_handle, false);
	nfit_mem->adev = adev_dimm;
	if (!adev_dimm) {
		dev_err(dev, "no ACPI.NFIT device with _ADR %#x, disabling...\n",
				device_handle);
		return force_enable_dimms ? 0 : -ENODEV;
	}

	if (ACPI_FAILURE(acpi_install_notify_handler(adev_dimm->handle,
		ACPI_DEVICE_NOTIFY, acpi_nvdimm_notify, adev_dimm))) {
		dev_err(dev, "%s: notification registration failed\n",
				dev_name(&adev_dimm->dev));
		return -ENXIO;
	}
	/*
	 * Record nfit_mem for the notification path to track back to
	 * the nfit sysfs attributes for this dimm device object.
	 */
	dev_set_drvdata(&adev_dimm->dev, nfit_mem);

	/*
	 * There are 4 "legacy" NVDIMM command sets
	 * (NVDIMM_FAMILY_{INTEL,MSFT,HPE1,HPE2}) that were created before
	 * an EFI working group was established to constrain this
	 * proliferation. The nfit driver probes for the supported command
	 * set by GUID. Note, if you're a platform developer looking to add
	 * a new command set to this probe, consider using an existing set,
	 * or otherwise seek approval to publish the command set at
	 * http://www.uefi.org/RFIC_LIST.
	 *
	 * Note, that checking for function0 (bit0) tells us if any commands
	 * are reachable through this GUID.
	 */
	clear_bit(NVDIMM_FAMILY_INTEL, &nd_desc->dimm_family_mask);
	for (i = 0; i <= NVDIMM_FAMILY_MAX; i++)
		if (acpi_check_dsm(adev_dimm->handle, to_nfit_uuid(i), 1, 1)) {
			set_bit(i, &nd_desc->dimm_family_mask);
			if (family < 0 || i == default_dsm_family)
				family = i;
		}

	/* limit the supported commands to those that are publicly documented */
	nfit_mem->family = family;
	if (override_dsm_mask && !disable_vendor_specific)
		dsm_mask = override_dsm_mask;
	else if (nfit_mem->family == NVDIMM_FAMILY_INTEL) {
		dsm_mask = NVDIMM_INTEL_CMDMASK;
		if (disable_vendor_specific)
			dsm_mask &= ~(1 << ND_CMD_VENDOR);
	} else if (nfit_mem->family == NVDIMM_FAMILY_HPE1) {
		dsm_mask = 0x1c3c76;
	} else if (nfit_mem->family == NVDIMM_FAMILY_HPE2) {
		dsm_mask = 0x1fe;
		if (disable_vendor_specific)
			dsm_mask &= ~(1 << 8);
	} else if (nfit_mem->family == NVDIMM_FAMILY_MSFT) {
		dsm_mask = 0xffffffff;
	} else if (nfit_mem->family == NVDIMM_FAMILY_HYPERV) {
		dsm_mask = 0x1f;
	} else {
		dev_dbg(dev, "unknown dimm command family\n");
		nfit_mem->family = -1;
		/* DSMs are optional, continue loading the driver... */
		return 0;
	}

	/*
	 * Function 0 is the command interrogation function, don't
	 * export it to potential userspace use, and enable it to be
	 * used as an error value in acpi_nfit_ctl().
	 */
	dsm_mask &= ~1UL;

	guid = to_nfit_uuid(nfit_mem->family);
	for_each_set_bit(i, &dsm_mask, BITS_PER_LONG)
		if (acpi_check_dsm(adev_dimm->handle, guid,
					nfit_dsm_revid(nfit_mem->family, i),
					1ULL << i))
			set_bit(i, &nfit_mem->dsm_mask);

	/*
	 * Prefer the NVDIMM_FAMILY_INTEL label read commands if present
	 * due to their better semantics handling locked capacity.
	 */
	label_mask = 1 << ND_CMD_GET_CONFIG_SIZE | 1 << ND_CMD_GET_CONFIG_DATA
		| 1 << ND_CMD_SET_CONFIG_DATA;
	if (family == NVDIMM_FAMILY_INTEL
			&& (dsm_mask & label_mask) == label_mask)
		/* skip _LS{I,R,W} enabling */;
	else {
		if (acpi_nvdimm_has_method(adev_dimm, "_LSI")
				&& acpi_nvdimm_has_method(adev_dimm, "_LSR")) {
			dev_dbg(dev, "%s: has _LSR\n", dev_name(&adev_dimm->dev));
			set_bit(NFIT_MEM_LSR, &nfit_mem->flags);
		}

		if (test_bit(NFIT_MEM_LSR, &nfit_mem->flags)
				&& acpi_nvdimm_has_method(adev_dimm, "_LSW")) {
			dev_dbg(dev, "%s: has _LSW\n", dev_name(&adev_dimm->dev));
			set_bit(NFIT_MEM_LSW, &nfit_mem->flags);
		}

		/*
		 * Quirk read-only label configurations to preserve
		 * access to label-less namespaces by default.
		 */
		if (!test_bit(NFIT_MEM_LSW, &nfit_mem->flags)
				&& !force_labels) {
			dev_dbg(dev, "%s: No _LSW, disable labels\n",
					dev_name(&adev_dimm->dev));
			clear_bit(NFIT_MEM_LSR, &nfit_mem->flags);
		} else
			dev_dbg(dev, "%s: Force enable labels\n",
					dev_name(&adev_dimm->dev));
	}

	populate_shutdown_status(nfit_mem);

	return 0;
}

static void shutdown_dimm_notify(void *data)
{
	struct acpi_nfit_desc *acpi_desc = data;
	struct nfit_mem *nfit_mem;

	mutex_lock(&acpi_desc->init_mutex);
	/*
	 * Clear out the nfit_mem->flags_attr and shut down dimm event
	 * notifications.
	 */
	list_for_each_entry(nfit_mem, &acpi_desc->dimms, list) {
		struct acpi_device *adev_dimm = nfit_mem->adev;

		if (nfit_mem->flags_attr) {
			sysfs_put(nfit_mem->flags_attr);
			nfit_mem->flags_attr = NULL;
		}
		if (adev_dimm) {
			acpi_remove_notify_handler(adev_dimm->handle,
					ACPI_DEVICE_NOTIFY, acpi_nvdimm_notify);
			dev_set_drvdata(&adev_dimm->dev, NULL);
		}
	}
	mutex_unlock(&acpi_desc->init_mutex);
}

static const struct nvdimm_security_ops *acpi_nfit_get_security_ops(int family)
{
	switch (family) {
	case NVDIMM_FAMILY_INTEL:
		return intel_security_ops;
	default:
		return NULL;
	}
}

static const struct nvdimm_fw_ops *acpi_nfit_get_fw_ops(
		struct nfit_mem *nfit_mem)
{
	unsigned long mask;
	struct acpi_nfit_desc *acpi_desc = nfit_mem->acpi_desc;
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;

	if (!nd_desc->fw_ops)
		return NULL;

	if (nfit_mem->family != NVDIMM_FAMILY_INTEL)
		return NULL;

	mask = nfit_mem->dsm_mask & NVDIMM_INTEL_FW_ACTIVATE_CMDMASK;
	if (mask != NVDIMM_INTEL_FW_ACTIVATE_CMDMASK)
		return NULL;

	return intel_fw_ops;
}

static int acpi_nfit_register_dimms(struct acpi_nfit_desc *acpi_desc)
{
	struct nfit_mem *nfit_mem;
	int dimm_count = 0, rc;
	struct nvdimm *nvdimm;

	list_for_each_entry(nfit_mem, &acpi_desc->dimms, list) {
		struct acpi_nfit_flush_address *flush;
		unsigned long flags = 0, cmd_mask;
		struct nfit_memdev *nfit_memdev;
		u32 device_handle;
		u16 mem_flags;

		device_handle = __to_nfit_memdev(nfit_mem)->device_handle;
		nvdimm = acpi_nfit_dimm_by_handle(acpi_desc, device_handle);
		if (nvdimm) {
			dimm_count++;
			continue;
		}

		if (nfit_mem->bdw && nfit_mem->memdev_pmem) {
			set_bit(NDD_ALIASING, &flags);
			set_bit(NDD_LABELING, &flags);
		}

		/* collate flags across all memdevs for this dimm */
		list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
			struct acpi_nfit_memory_map *dimm_memdev;

			dimm_memdev = __to_nfit_memdev(nfit_mem);
			if (dimm_memdev->device_handle
					!= nfit_memdev->memdev->device_handle)
				continue;
			dimm_memdev->flags |= nfit_memdev->memdev->flags;
		}

		mem_flags = __to_nfit_memdev(nfit_mem)->flags;
		if (mem_flags & ACPI_NFIT_MEM_NOT_ARMED)
			set_bit(NDD_UNARMED, &flags);

		rc = acpi_nfit_add_dimm(acpi_desc, nfit_mem, device_handle);
		if (rc)
			continue;

		/*
		 * TODO: provide translation for non-NVDIMM_FAMILY_INTEL
		 * devices (i.e. from nd_cmd to acpi_dsm) to standardize the
		 * userspace interface.
		 */
		cmd_mask = 1UL << ND_CMD_CALL;
		if (nfit_mem->family == NVDIMM_FAMILY_INTEL) {
			/*
			 * These commands have a 1:1 correspondence
			 * between DSM payload and libnvdimm ioctl
			 * payload format.
			 */
			cmd_mask |= nfit_mem->dsm_mask & NVDIMM_STANDARD_CMDMASK;
		}

		/* Quirk to ignore LOCAL for labels on HYPERV DIMMs */
		if (nfit_mem->family == NVDIMM_FAMILY_HYPERV)
			set_bit(NDD_NOBLK, &flags);

		if (test_bit(NFIT_MEM_LSR, &nfit_mem->flags)) {
			set_bit(ND_CMD_GET_CONFIG_SIZE, &cmd_mask);
			set_bit(ND_CMD_GET_CONFIG_DATA, &cmd_mask);
		}
		if (test_bit(NFIT_MEM_LSW, &nfit_mem->flags))
			set_bit(ND_CMD_SET_CONFIG_DATA, &cmd_mask);

		flush = nfit_mem->nfit_flush ? nfit_mem->nfit_flush->flush
			: NULL;
		nvdimm = __nvdimm_create(acpi_desc->nvdimm_bus, nfit_mem,
				acpi_nfit_dimm_attribute_groups,
				flags, cmd_mask, flush ? flush->hint_count : 0,
				nfit_mem->flush_wpq, &nfit_mem->id[0],
				acpi_nfit_get_security_ops(nfit_mem->family),
				acpi_nfit_get_fw_ops(nfit_mem));
		if (!nvdimm)
			return -ENOMEM;

		nfit_mem->nvdimm = nvdimm;
		dimm_count++;

		if ((mem_flags & ACPI_NFIT_MEM_FAILED_MASK) == 0)
			continue;

		dev_err(acpi_desc->dev, "Error found in NVDIMM %s flags:%s%s%s%s%s\n",
				nvdimm_name(nvdimm),
		  mem_flags & ACPI_NFIT_MEM_SAVE_FAILED ? " save_fail" : "",
		  mem_flags & ACPI_NFIT_MEM_RESTORE_FAILED ? " restore_fail":"",
		  mem_flags & ACPI_NFIT_MEM_FLUSH_FAILED ? " flush_fail" : "",
		  mem_flags & ACPI_NFIT_MEM_NOT_ARMED ? " not_armed" : "",
		  mem_flags & ACPI_NFIT_MEM_MAP_FAILED ? " map_fail" : "");

	}

	rc = nvdimm_bus_check_dimm_count(acpi_desc->nvdimm_bus, dimm_count);
	if (rc)
		return rc;

	/*
	 * Now that dimms are successfully registered, and async registration
	 * is flushed, attempt to enable event notification.
	 */
	list_for_each_entry(nfit_mem, &acpi_desc->dimms, list) {
		struct kernfs_node *nfit_kernfs;

		nvdimm = nfit_mem->nvdimm;
		if (!nvdimm)
			continue;

		nfit_kernfs = sysfs_get_dirent(nvdimm_kobj(nvdimm)->sd, "nfit");
		if (nfit_kernfs)
			nfit_mem->flags_attr = sysfs_get_dirent(nfit_kernfs,
					"flags");
		sysfs_put(nfit_kernfs);
		if (!nfit_mem->flags_attr)
			dev_warn(acpi_desc->dev, "%s: notifications disabled\n",
					nvdimm_name(nvdimm));
	}

	return devm_add_action_or_reset(acpi_desc->dev, shutdown_dimm_notify,
			acpi_desc);
}

/*
 * These constants are private because there are no kernel consumers of
 * these commands.
 */
enum nfit_aux_cmds {
	NFIT_CMD_TRANSLATE_SPA = 5,
	NFIT_CMD_ARS_INJECT_SET = 7,
	NFIT_CMD_ARS_INJECT_CLEAR = 8,
	NFIT_CMD_ARS_INJECT_GET = 9,
};

static void acpi_nfit_init_dsms(struct acpi_nfit_desc *acpi_desc)
{
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;
	const guid_t *guid = to_nfit_uuid(NFIT_DEV_BUS);
	unsigned long dsm_mask, *mask;
	struct acpi_device *adev;
	int i;

	set_bit(ND_CMD_CALL, &nd_desc->cmd_mask);
	set_bit(NVDIMM_BUS_FAMILY_NFIT, &nd_desc->bus_family_mask);

	/* enable nfit_test to inject bus command emulation */
	if (acpi_desc->bus_cmd_force_en) {
		nd_desc->cmd_mask = acpi_desc->bus_cmd_force_en;
		mask = &nd_desc->bus_family_mask;
		if (acpi_desc->family_dsm_mask[NVDIMM_BUS_FAMILY_INTEL]) {
			set_bit(NVDIMM_BUS_FAMILY_INTEL, mask);
			nd_desc->fw_ops = intel_bus_fw_ops;
		}
	}

	adev = to_acpi_dev(acpi_desc);
	if (!adev)
		return;

	for (i = ND_CMD_ARS_CAP; i <= ND_CMD_CLEAR_ERROR; i++)
		if (acpi_check_dsm(adev->handle, guid, 1, 1ULL << i))
			set_bit(i, &nd_desc->cmd_mask);

	dsm_mask =
		(1 << ND_CMD_ARS_CAP) |
		(1 << ND_CMD_ARS_START) |
		(1 << ND_CMD_ARS_STATUS) |
		(1 << ND_CMD_CLEAR_ERROR) |
		(1 << NFIT_CMD_TRANSLATE_SPA) |
		(1 << NFIT_CMD_ARS_INJECT_SET) |
		(1 << NFIT_CMD_ARS_INJECT_CLEAR) |
		(1 << NFIT_CMD_ARS_INJECT_GET);
	for_each_set_bit(i, &dsm_mask, BITS_PER_LONG)
		if (acpi_check_dsm(adev->handle, guid, 1, 1ULL << i))
			set_bit(i, &acpi_desc->bus_dsm_mask);

	/* Enumerate allowed NVDIMM_BUS_FAMILY_INTEL commands */
	dsm_mask = NVDIMM_BUS_INTEL_FW_ACTIVATE_CMDMASK;
	guid = to_nfit_bus_uuid(NVDIMM_BUS_FAMILY_INTEL);
	mask = &acpi_desc->family_dsm_mask[NVDIMM_BUS_FAMILY_INTEL];
	for_each_set_bit(i, &dsm_mask, BITS_PER_LONG)
		if (acpi_check_dsm(adev->handle, guid, 1, 1ULL << i))
			set_bit(i, mask);

	if (*mask == dsm_mask) {
		set_bit(NVDIMM_BUS_FAMILY_INTEL, &nd_desc->bus_family_mask);
		nd_desc->fw_ops = intel_bus_fw_ops;
	}
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

static const struct attribute_group acpi_nfit_region_attribute_group = {
	.name = "nfit",
	.attrs = acpi_nfit_region_attributes,
};

static const struct attribute_group *acpi_nfit_region_attribute_groups[] = {
	&acpi_nfit_region_attribute_group,
	NULL,
};

/* enough info to uniquely specify an interleave set */
struct nfit_set_info {
	u64 region_offset;
	u32 serial_number;
	u32 pad;
};

struct nfit_set_info2 {
	u64 region_offset;
	u32 serial_number;
	u16 vendor_id;
	u16 manufacturing_date;
	u8 manufacturing_location;
	u8 reserved[31];
};

static int cmp_map_compat(const void *m0, const void *m1)
{
	const struct nfit_set_info *map0 = m0;
	const struct nfit_set_info *map1 = m1;

	return memcmp(&map0->region_offset, &map1->region_offset,
			sizeof(u64));
}

static int cmp_map(const void *m0, const void *m1)
{
	const struct nfit_set_info *map0 = m0;
	const struct nfit_set_info *map1 = m1;

	if (map0->region_offset < map1->region_offset)
		return -1;
	else if (map0->region_offset > map1->region_offset)
		return 1;
	return 0;
}

static int cmp_map2(const void *m0, const void *m1)
{
	const struct nfit_set_info2 *map0 = m0;
	const struct nfit_set_info2 *map1 = m1;

	if (map0->region_offset < map1->region_offset)
		return -1;
	else if (map0->region_offset > map1->region_offset)
		return 1;
	return 0;
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
	struct device *dev = acpi_desc->dev;
	struct nd_interleave_set *nd_set;
	u16 nr = ndr_desc->num_mappings;
	struct nfit_set_info2 *info2;
	struct nfit_set_info *info;
	int i;

	nd_set = devm_kzalloc(dev, sizeof(*nd_set), GFP_KERNEL);
	if (!nd_set)
		return -ENOMEM;
	import_guid(&nd_set->type_guid, spa->range_guid);

	info = devm_kcalloc(dev, nr, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info2 = devm_kcalloc(dev, nr, sizeof(*info2), GFP_KERNEL);
	if (!info2)
		return -ENOMEM;

	for (i = 0; i < nr; i++) {
		struct nd_mapping_desc *mapping = &ndr_desc->mapping[i];
		struct nvdimm *nvdimm = mapping->nvdimm;
		struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
		struct nfit_set_info *map = &info[i];
		struct nfit_set_info2 *map2 = &info2[i];
		struct acpi_nfit_memory_map *memdev =
			memdev_from_spa(acpi_desc, spa->range_index, i);
		struct acpi_nfit_control_region *dcr = nfit_mem->dcr;

		if (!memdev || !nfit_mem->dcr) {
			dev_err(dev, "%s: failed to find DCR\n", __func__);
			return -ENODEV;
		}

		map->region_offset = memdev->region_offset;
		map->serial_number = dcr->serial_number;

		map2->region_offset = memdev->region_offset;
		map2->serial_number = dcr->serial_number;
		map2->vendor_id = dcr->vendor_id;
		map2->manufacturing_date = dcr->manufacturing_date;
		map2->manufacturing_location = dcr->manufacturing_location;
	}

	/* v1.1 namespaces */
	sort(info, nr, sizeof(*info), cmp_map, NULL);
	nd_set->cookie1 = nd_fletcher64(info, sizeof(*info) * nr, 0);

	/* v1.2 namespaces */
	sort(info2, nr, sizeof(*info2), cmp_map2, NULL);
	nd_set->cookie2 = nd_fletcher64(info2, sizeof(*info2) * nr, 0);

	/* support v1.1 namespaces created with the wrong sort order */
	sort(info, nr, sizeof(*info), cmp_map_compat, NULL);
	nd_set->altcookie = nd_fletcher64(info, sizeof(*info) * nr, 0);

	/* record the result of the sort for the mapping position */
	for (i = 0; i < nr; i++) {
		struct nfit_set_info2 *map2 = &info2[i];
		int j;

		for (j = 0; j < nr; j++) {
			struct nd_mapping_desc *mapping = &ndr_desc->mapping[j];
			struct nvdimm *nvdimm = mapping->nvdimm;
			struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
			struct acpi_nfit_control_region *dcr = nfit_mem->dcr;

			if (map2->serial_number == dcr->serial_number &&
			    map2->vendor_id == dcr->vendor_id &&
			    map2->manufacturing_date == dcr->manufacturing_date &&
			    map2->manufacturing_location
				    == dcr->manufacturing_location) {
				mapping->position = i;
				break;
			}
		}
	}

	ndr_desc->nd_set = nd_set;
	devm_kfree(dev, info);
	devm_kfree(dev, info2);

	return 0;
}

static u64 to_interleave_offset(u64 offset, struct nfit_blk_mmio *mmio)
{
	struct acpi_nfit_interleave *idt = mmio->idt;
	u32 sub_line_offset, line_index, line_offset;
	u64 line_no, table_skip_count, table_offset;

	line_no = div_u64_rem(offset, mmio->line_size, &sub_line_offset);
	table_skip_count = div_u64_rem(line_no, mmio->num_lines, &line_index);
	line_offset = idt->line_offset[line_index]
		* mmio->line_size;
	table_offset = table_skip_count * mmio->table_size;

	return mmio->base_offset + line_offset + table_offset + sub_line_offset;
}

static u32 read_blk_stat(struct nfit_blk *nfit_blk, unsigned int bw)
{
	struct nfit_blk_mmio *mmio = &nfit_blk->mmio[DCR];
	u64 offset = nfit_blk->stat_offset + mmio->size * bw;
	const u32 STATUS_MASK = 0x80000037;

	if (mmio->num_lines)
		offset = to_interleave_offset(offset, mmio);

	return readl(mmio->addr.base + offset) & STATUS_MASK;
}

static void write_blk_ctl(struct nfit_blk *nfit_blk, unsigned int bw,
		resource_size_t dpa, unsigned int len, unsigned int write)
{
	u64 cmd, offset;
	struct nfit_blk_mmio *mmio = &nfit_blk->mmio[DCR];

	enum {
		BCW_OFFSET_MASK = (1ULL << 48)-1,
		BCW_LEN_SHIFT = 48,
		BCW_LEN_MASK = (1ULL << 8) - 1,
		BCW_CMD_SHIFT = 56,
	};

	cmd = (dpa >> L1_CACHE_SHIFT) & BCW_OFFSET_MASK;
	len = len >> L1_CACHE_SHIFT;
	cmd |= ((u64) len & BCW_LEN_MASK) << BCW_LEN_SHIFT;
	cmd |= ((u64) write) << BCW_CMD_SHIFT;

	offset = nfit_blk->cmd_offset + mmio->size * bw;
	if (mmio->num_lines)
		offset = to_interleave_offset(offset, mmio);

	writeq(cmd, mmio->addr.base + offset);
	nvdimm_flush(nfit_blk->nd_region, NULL);

	if (nfit_blk->dimm_flags & NFIT_BLK_DCR_LATCH)
		readq(mmio->addr.base + offset);
}

static int acpi_nfit_blk_single_io(struct nfit_blk *nfit_blk,
		resource_size_t dpa, void *iobuf, size_t len, int rw,
		unsigned int lane)
{
	struct nfit_blk_mmio *mmio = &nfit_blk->mmio[BDW];
	unsigned int copied = 0;
	u64 base_offset;
	int rc;

	base_offset = nfit_blk->bdw_offset + dpa % L1_CACHE_BYTES
		+ lane * mmio->size;
	write_blk_ctl(nfit_blk, lane, dpa, len, rw);
	while (len) {
		unsigned int c;
		u64 offset;

		if (mmio->num_lines) {
			u32 line_offset;

			offset = to_interleave_offset(base_offset + copied,
					mmio);
			div_u64_rem(offset, mmio->line_size, &line_offset);
			c = min_t(size_t, len, mmio->line_size - line_offset);
		} else {
			offset = base_offset + nfit_blk->bdw_offset;
			c = len;
		}

		if (rw)
			memcpy_flushcache(mmio->addr.aperture + offset, iobuf + copied, c);
		else {
			if (nfit_blk->dimm_flags & NFIT_BLK_READ_FLUSH)
				arch_invalidate_pmem((void __force *)
					mmio->addr.aperture + offset, c);

			memcpy(iobuf + copied, mmio->addr.aperture + offset, c);
		}

		copied += c;
		len -= c;
	}

	if (rw)
		nvdimm_flush(nfit_blk->nd_region, NULL);

	rc = read_blk_stat(nfit_blk, lane) ? -EIO : 0;
	return rc;
}

static int acpi_nfit_blk_region_do_io(struct nd_blk_region *ndbr,
		resource_size_t dpa, void *iobuf, u64 len, int rw)
{
	struct nfit_blk *nfit_blk = nd_blk_region_provider_data(ndbr);
	struct nfit_blk_mmio *mmio = &nfit_blk->mmio[BDW];
	struct nd_region *nd_region = nfit_blk->nd_region;
	unsigned int lane, copied = 0;
	int rc = 0;

	lane = nd_region_acquire_lane(nd_region);
	while (len) {
		u64 c = min(len, mmio->size);

		rc = acpi_nfit_blk_single_io(nfit_blk, dpa + copied,
				iobuf + copied, c, rw, lane);
		if (rc)
			break;

		copied += c;
		len -= c;
	}
	nd_region_release_lane(nd_region, lane);

	return rc;
}

static int nfit_blk_init_interleave(struct nfit_blk_mmio *mmio,
		struct acpi_nfit_interleave *idt, u16 interleave_ways)
{
	if (idt) {
		mmio->num_lines = idt->line_count;
		mmio->line_size = idt->line_size;
		if (interleave_ways == 0)
			return -ENXIO;
		mmio->table_size = mmio->num_lines * interleave_ways
			* mmio->line_size;
	}

	return 0;
}

static int acpi_nfit_blk_get_flags(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, struct nfit_blk *nfit_blk)
{
	struct nd_cmd_dimm_flags flags;
	int rc;

	memset(&flags, 0, sizeof(flags));
	rc = nd_desc->ndctl(nd_desc, nvdimm, ND_CMD_DIMM_FLAGS, &flags,
			sizeof(flags), NULL);

	if (rc >= 0 && flags.status == 0)
		nfit_blk->dimm_flags = flags.flags;
	else if (rc == -ENOTTY) {
		/* fall back to a conservative default */
		nfit_blk->dimm_flags = NFIT_BLK_DCR_LATCH | NFIT_BLK_READ_FLUSH;
		rc = 0;
	} else
		rc = -ENXIO;

	return rc;
}

static int acpi_nfit_blk_region_enable(struct nvdimm_bus *nvdimm_bus,
		struct device *dev)
{
	struct nvdimm_bus_descriptor *nd_desc = to_nd_desc(nvdimm_bus);
	struct nd_blk_region *ndbr = to_nd_blk_region(dev);
	struct nfit_blk_mmio *mmio;
	struct nfit_blk *nfit_blk;
	struct nfit_mem *nfit_mem;
	struct nvdimm *nvdimm;
	int rc;

	nvdimm = nd_blk_region_to_dimm(ndbr);
	nfit_mem = nvdimm_provider_data(nvdimm);
	if (!nfit_mem || !nfit_mem->dcr || !nfit_mem->bdw) {
		dev_dbg(dev, "missing%s%s%s\n",
				nfit_mem ? "" : " nfit_mem",
				(nfit_mem && nfit_mem->dcr) ? "" : " dcr",
				(nfit_mem && nfit_mem->bdw) ? "" : " bdw");
		return -ENXIO;
	}

	nfit_blk = devm_kzalloc(dev, sizeof(*nfit_blk), GFP_KERNEL);
	if (!nfit_blk)
		return -ENOMEM;
	nd_blk_region_set_provider_data(ndbr, nfit_blk);
	nfit_blk->nd_region = to_nd_region(dev);

	/* map block aperture memory */
	nfit_blk->bdw_offset = nfit_mem->bdw->offset;
	mmio = &nfit_blk->mmio[BDW];
	mmio->addr.base = devm_nvdimm_memremap(dev, nfit_mem->spa_bdw->address,
			nfit_mem->spa_bdw->length, nd_blk_memremap_flags(ndbr));
	if (!mmio->addr.base) {
		dev_dbg(dev, "%s failed to map bdw\n",
				nvdimm_name(nvdimm));
		return -ENOMEM;
	}
	mmio->size = nfit_mem->bdw->size;
	mmio->base_offset = nfit_mem->memdev_bdw->region_offset;
	mmio->idt = nfit_mem->idt_bdw;
	mmio->spa = nfit_mem->spa_bdw;
	rc = nfit_blk_init_interleave(mmio, nfit_mem->idt_bdw,
			nfit_mem->memdev_bdw->interleave_ways);
	if (rc) {
		dev_dbg(dev, "%s failed to init bdw interleave\n",
				nvdimm_name(nvdimm));
		return rc;
	}

	/* map block control memory */
	nfit_blk->cmd_offset = nfit_mem->dcr->command_offset;
	nfit_blk->stat_offset = nfit_mem->dcr->status_offset;
	mmio = &nfit_blk->mmio[DCR];
	mmio->addr.base = devm_nvdimm_ioremap(dev, nfit_mem->spa_dcr->address,
			nfit_mem->spa_dcr->length);
	if (!mmio->addr.base) {
		dev_dbg(dev, "%s failed to map dcr\n",
				nvdimm_name(nvdimm));
		return -ENOMEM;
	}
	mmio->size = nfit_mem->dcr->window_size;
	mmio->base_offset = nfit_mem->memdev_dcr->region_offset;
	mmio->idt = nfit_mem->idt_dcr;
	mmio->spa = nfit_mem->spa_dcr;
	rc = nfit_blk_init_interleave(mmio, nfit_mem->idt_dcr,
			nfit_mem->memdev_dcr->interleave_ways);
	if (rc) {
		dev_dbg(dev, "%s failed to init dcr interleave\n",
				nvdimm_name(nvdimm));
		return rc;
	}

	rc = acpi_nfit_blk_get_flags(nd_desc, nvdimm, nfit_blk);
	if (rc < 0) {
		dev_dbg(dev, "%s failed get DIMM flags\n",
				nvdimm_name(nvdimm));
		return rc;
	}

	if (nvdimm_has_flush(nfit_blk->nd_region) < 0)
		dev_warn(dev, "unable to guarantee persistence of writes\n");

	if (mmio->line_size == 0)
		return 0;

	if ((u32) nfit_blk->cmd_offset % mmio->line_size
			+ 8 > mmio->line_size) {
		dev_dbg(dev, "cmd_offset crosses interleave boundary\n");
		return -ENXIO;
	} else if ((u32) nfit_blk->stat_offset % mmio->line_size
			+ 8 > mmio->line_size) {
		dev_dbg(dev, "stat_offset crosses interleave boundary\n");
		return -ENXIO;
	}

	return 0;
}

static int ars_get_cap(struct acpi_nfit_desc *acpi_desc,
		struct nd_cmd_ars_cap *cmd, struct nfit_spa *nfit_spa)
{
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;
	struct acpi_nfit_system_address *spa = nfit_spa->spa;
	int cmd_rc, rc;

	cmd->address = spa->address;
	cmd->length = spa->length;
	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_ARS_CAP, cmd,
			sizeof(*cmd), &cmd_rc);
	if (rc < 0)
		return rc;
	return cmd_rc;
}

static int ars_start(struct acpi_nfit_desc *acpi_desc,
		struct nfit_spa *nfit_spa, enum nfit_ars_state req_type)
{
	int rc;
	int cmd_rc;
	struct nd_cmd_ars_start ars_start;
	struct acpi_nfit_system_address *spa = nfit_spa->spa;
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;

	memset(&ars_start, 0, sizeof(ars_start));
	ars_start.address = spa->address;
	ars_start.length = spa->length;
	if (req_type == ARS_REQ_SHORT)
		ars_start.flags = ND_ARS_RETURN_PREV_DATA;
	if (nfit_spa_type(spa) == NFIT_SPA_PM)
		ars_start.type = ND_ARS_PERSISTENT;
	else if (nfit_spa_type(spa) == NFIT_SPA_VOLATILE)
		ars_start.type = ND_ARS_VOLATILE;
	else
		return -ENOTTY;

	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_ARS_START, &ars_start,
			sizeof(ars_start), &cmd_rc);

	if (rc < 0)
		return rc;
	if (cmd_rc < 0)
		return cmd_rc;
	set_bit(ARS_VALID, &acpi_desc->scrub_flags);
	return 0;
}

static int ars_continue(struct acpi_nfit_desc *acpi_desc)
{
	int rc, cmd_rc;
	struct nd_cmd_ars_start ars_start;
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;
	struct nd_cmd_ars_status *ars_status = acpi_desc->ars_status;

	ars_start = (struct nd_cmd_ars_start) {
		.address = ars_status->restart_address,
		.length = ars_status->restart_length,
		.type = ars_status->type,
	};
	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_ARS_START, &ars_start,
			sizeof(ars_start), &cmd_rc);
	if (rc < 0)
		return rc;
	return cmd_rc;
}

static int ars_get_status(struct acpi_nfit_desc *acpi_desc)
{
	struct nvdimm_bus_descriptor *nd_desc = &acpi_desc->nd_desc;
	struct nd_cmd_ars_status *ars_status = acpi_desc->ars_status;
	int rc, cmd_rc;

	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_ARS_STATUS, ars_status,
			acpi_desc->max_ars, &cmd_rc);
	if (rc < 0)
		return rc;
	return cmd_rc;
}

static void ars_complete(struct acpi_nfit_desc *acpi_desc,
		struct nfit_spa *nfit_spa)
{
	struct nd_cmd_ars_status *ars_status = acpi_desc->ars_status;
	struct acpi_nfit_system_address *spa = nfit_spa->spa;
	struct nd_region *nd_region = nfit_spa->nd_region;
	struct device *dev;

	lockdep_assert_held(&acpi_desc->init_mutex);
	/*
	 * Only advance the ARS state for ARS runs initiated by the
	 * kernel, ignore ARS results from BIOS initiated runs for scrub
	 * completion tracking.
	 */
	if (acpi_desc->scrub_spa != nfit_spa)
		return;

	if ((ars_status->address >= spa->address && ars_status->address
				< spa->address + spa->length)
			|| (ars_status->address < spa->address)) {
		/*
		 * Assume that if a scrub starts at an offset from the
		 * start of nfit_spa that we are in the continuation
		 * case.
		 *
		 * Otherwise, if the scrub covers the spa range, mark
		 * any pending request complete.
		 */
		if (ars_status->address + ars_status->length
				>= spa->address + spa->length)
				/* complete */;
		else
			return;
	} else
		return;

	acpi_desc->scrub_spa = NULL;
	if (nd_region) {
		dev = nd_region_dev(nd_region);
		nvdimm_region_notify(nd_region, NVDIMM_REVALIDATE_POISON);
	} else
		dev = acpi_desc->dev;
	dev_dbg(dev, "ARS: range %d complete\n", spa->range_index);
}

static int ars_status_process_records(struct acpi_nfit_desc *acpi_desc)
{
	struct nvdimm_bus *nvdimm_bus = acpi_desc->nvdimm_bus;
	struct nd_cmd_ars_status *ars_status = acpi_desc->ars_status;
	int rc;
	u32 i;

	/*
	 * First record starts at 44 byte offset from the start of the
	 * payload.
	 */
	if (ars_status->out_length < 44)
		return 0;

	/*
	 * Ignore potentially stale results that are only refreshed
	 * after a start-ARS event.
	 */
	if (!test_and_clear_bit(ARS_VALID, &acpi_desc->scrub_flags)) {
		dev_dbg(acpi_desc->dev, "skip %d stale records\n",
				ars_status->num_records);
		return 0;
	}

	for (i = 0; i < ars_status->num_records; i++) {
		/* only process full records */
		if (ars_status->out_length
				< 44 + sizeof(struct nd_ars_record) * (i + 1))
			break;
		rc = nvdimm_bus_add_badrange(nvdimm_bus,
				ars_status->records[i].err_address,
				ars_status->records[i].length);
		if (rc)
			return rc;
	}
	if (i < ars_status->num_records)
		dev_warn(acpi_desc->dev, "detected truncated ars results\n");

	return 0;
}

static void acpi_nfit_remove_resource(void *data)
{
	struct resource *res = data;

	remove_resource(res);
}

static int acpi_nfit_insert_resource(struct acpi_nfit_desc *acpi_desc,
		struct nd_region_desc *ndr_desc)
{
	struct resource *res, *nd_res = ndr_desc->res;
	int is_pmem, ret;

	/* No operation if the region is already registered as PMEM */
	is_pmem = region_intersects(nd_res->start, resource_size(nd_res),
				IORESOURCE_MEM, IORES_DESC_PERSISTENT_MEMORY);
	if (is_pmem == REGION_INTERSECTS)
		return 0;

	res = devm_kzalloc(acpi_desc->dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	res->name = "Persistent Memory";
	res->start = nd_res->start;
	res->end = nd_res->end;
	res->flags = IORESOURCE_MEM;
	res->desc = IORES_DESC_PERSISTENT_MEMORY;

	ret = insert_resource(&iomem_resource, res);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(acpi_desc->dev,
					acpi_nfit_remove_resource,
					res);
	if (ret)
		return ret;

	return 0;
}

static int acpi_nfit_init_mapping(struct acpi_nfit_desc *acpi_desc,
		struct nd_mapping_desc *mapping, struct nd_region_desc *ndr_desc,
		struct acpi_nfit_memory_map *memdev,
		struct nfit_spa *nfit_spa)
{
	struct nvdimm *nvdimm = acpi_nfit_dimm_by_handle(acpi_desc,
			memdev->device_handle);
	struct acpi_nfit_system_address *spa = nfit_spa->spa;
	struct nd_blk_region_desc *ndbr_desc;
	struct nfit_mem *nfit_mem;
	int rc;

	if (!nvdimm) {
		dev_err(acpi_desc->dev, "spa%d dimm: %#x not found\n",
				spa->range_index, memdev->device_handle);
		return -ENODEV;
	}

	mapping->nvdimm = nvdimm;
	switch (nfit_spa_type(spa)) {
	case NFIT_SPA_PM:
	case NFIT_SPA_VOLATILE:
		mapping->start = memdev->address;
		mapping->size = memdev->region_size;
		break;
	case NFIT_SPA_DCR:
		nfit_mem = nvdimm_provider_data(nvdimm);
		if (!nfit_mem || !nfit_mem->bdw) {
			dev_dbg(acpi_desc->dev, "spa%d %s missing bdw\n",
					spa->range_index, nvdimm_name(nvdimm));
			break;
		}

		mapping->size = nfit_mem->bdw->capacity;
		mapping->start = nfit_mem->bdw->start_address;
		ndr_desc->num_lanes = nfit_mem->bdw->windows;
		ndr_desc->mapping = mapping;
		ndr_desc->num_mappings = 1;
		ndbr_desc = to_blk_region_desc(ndr_desc);
		ndbr_desc->enable = acpi_nfit_blk_region_enable;
		ndbr_desc->do_io = acpi_desc->blk_do_io;
		rc = acpi_nfit_init_interleave_set(acpi_desc, ndr_desc, spa);
		if (rc)
			return rc;
		nfit_spa->nd_region = nvdimm_blk_region_create(acpi_desc->nvdimm_bus,
				ndr_desc);
		if (!nfit_spa->nd_region)
			return -ENOMEM;
		break;
	}

	return 0;
}

static bool nfit_spa_is_virtual(struct acpi_nfit_system_address *spa)
{
	return (nfit_spa_type(spa) == NFIT_SPA_VDISK ||
		nfit_spa_type(spa) == NFIT_SPA_VCD   ||
		nfit_spa_type(spa) == NFIT_SPA_PDISK ||
		nfit_spa_type(spa) == NFIT_SPA_PCD);
}

static bool nfit_spa_is_volatile(struct acpi_nfit_system_address *spa)
{
	return (nfit_spa_type(spa) == NFIT_SPA_VDISK ||
		nfit_spa_type(spa) == NFIT_SPA_VCD   ||
		nfit_spa_type(spa) == NFIT_SPA_VOLATILE);
}

static int acpi_nfit_register_region(struct acpi_nfit_desc *acpi_desc,
		struct nfit_spa *nfit_spa)
{
	static struct nd_mapping_desc mappings[ND_MAX_MAPPINGS];
	struct acpi_nfit_system_address *spa = nfit_spa->spa;
	struct nd_blk_region_desc ndbr_desc;
	struct nd_region_desc *ndr_desc;
	struct nfit_memdev *nfit_memdev;
	struct nvdimm_bus *nvdimm_bus;
	struct resource res;
	int count = 0, rc;

	if (nfit_spa->nd_region)
		return 0;

	if (spa->range_index == 0 && !nfit_spa_is_virtual(spa)) {
		dev_dbg(acpi_desc->dev, "detected invalid spa index\n");
		return 0;
	}

	memset(&res, 0, sizeof(res));
	memset(&mappings, 0, sizeof(mappings));
	memset(&ndbr_desc, 0, sizeof(ndbr_desc));
	res.start = spa->address;
	res.end = res.start + spa->length - 1;
	ndr_desc = &ndbr_desc.ndr_desc;
	ndr_desc->res = &res;
	ndr_desc->provider_data = nfit_spa;
	ndr_desc->attr_groups = acpi_nfit_region_attribute_groups;
	if (spa->flags & ACPI_NFIT_PROXIMITY_VALID) {
		ndr_desc->numa_node = pxm_to_online_node(spa->proximity_domain);
		ndr_desc->target_node = pxm_to_node(spa->proximity_domain);
	} else {
		ndr_desc->numa_node = NUMA_NO_NODE;
		ndr_desc->target_node = NUMA_NO_NODE;
	}

	/*
	 * Persistence domain bits are hierarchical, if
	 * ACPI_NFIT_CAPABILITY_CACHE_FLUSH is set then
	 * ACPI_NFIT_CAPABILITY_MEM_FLUSH is implied.
	 */
	if (acpi_desc->platform_cap & ACPI_NFIT_CAPABILITY_CACHE_FLUSH)
		set_bit(ND_REGION_PERSIST_CACHE, &ndr_desc->flags);
	else if (acpi_desc->platform_cap & ACPI_NFIT_CAPABILITY_MEM_FLUSH)
		set_bit(ND_REGION_PERSIST_MEMCTRL, &ndr_desc->flags);

	list_for_each_entry(nfit_memdev, &acpi_desc->memdevs, list) {
		struct acpi_nfit_memory_map *memdev = nfit_memdev->memdev;
		struct nd_mapping_desc *mapping;

		/* range index 0 == unmapped in SPA or invalid-SPA */
		if (memdev->range_index == 0 || spa->range_index == 0)
			continue;
		if (memdev->range_index != spa->range_index)
			continue;
		if (count >= ND_MAX_MAPPINGS) {
			dev_err(acpi_desc->dev, "spa%d exceeds max mappings %d\n",
					spa->range_index, ND_MAX_MAPPINGS);
			return -ENXIO;
		}
		mapping = &mappings[count++];
		rc = acpi_nfit_init_mapping(acpi_desc, mapping, ndr_desc,
				memdev, nfit_spa);
		if (rc)
			goto out;
	}

	ndr_desc->mapping = mappings;
	ndr_desc->num_mappings = count;
	rc = acpi_nfit_init_interleave_set(acpi_desc, ndr_desc, spa);
	if (rc)
		goto out;

	nvdimm_bus = acpi_desc->nvdimm_bus;
	if (nfit_spa_type(spa) == NFIT_SPA_PM) {
		rc = acpi_nfit_insert_resource(acpi_desc, ndr_desc);
		if (rc) {
			dev_warn(acpi_desc->dev,
				"failed to insert pmem resource to iomem: %d\n",
				rc);
			goto out;
		}

		nfit_spa->nd_region = nvdimm_pmem_region_create(nvdimm_bus,
				ndr_desc);
		if (!nfit_spa->nd_region)
			rc = -ENOMEM;
	} else if (nfit_spa_is_volatile(spa)) {
		nfit_spa->nd_region = nvdimm_volatile_region_create(nvdimm_bus,
				ndr_desc);
		if (!nfit_spa->nd_region)
			rc = -ENOMEM;
	} else if (nfit_spa_is_virtual(spa)) {
		nfit_spa->nd_region = nvdimm_pmem_region_create(nvdimm_bus,
				ndr_desc);
		if (!nfit_spa->nd_region)
			rc = -ENOMEM;
	}

 out:
	if (rc)
		dev_err(acpi_desc->dev, "failed to register spa range %d\n",
				nfit_spa->spa->range_index);
	return rc;
}

static int ars_status_alloc(struct acpi_nfit_desc *acpi_desc)
{
	struct device *dev = acpi_desc->dev;
	struct nd_cmd_ars_status *ars_status;

	if (acpi_desc->ars_status) {
		memset(acpi_desc->ars_status, 0, acpi_desc->max_ars);
		return 0;
	}

	ars_status = devm_kzalloc(dev, acpi_desc->max_ars, GFP_KERNEL);
	if (!ars_status)
		return -ENOMEM;
	acpi_desc->ars_status = ars_status;
	return 0;
}

static int acpi_nfit_query_poison(struct acpi_nfit_desc *acpi_desc)
{
	int rc;

	if (ars_status_alloc(acpi_desc))
		return -ENOMEM;

	rc = ars_get_status(acpi_desc);

	if (rc < 0 && rc != -ENOSPC)
		return rc;

	if (ars_status_process_records(acpi_desc))
		dev_err(acpi_desc->dev, "Failed to process ARS records\n");

	return rc;
}

static int ars_register(struct acpi_nfit_desc *acpi_desc,
		struct nfit_spa *nfit_spa)
{
	int rc;

	if (test_bit(ARS_FAILED, &nfit_spa->ars_state))
		return acpi_nfit_register_region(acpi_desc, nfit_spa);

	set_bit(ARS_REQ_SHORT, &nfit_spa->ars_state);
	if (!no_init_ars)
		set_bit(ARS_REQ_LONG, &nfit_spa->ars_state);

	switch (acpi_nfit_query_poison(acpi_desc)) {
	case 0:
	case -ENOSPC:
	case -EAGAIN:
		rc = ars_start(acpi_desc, nfit_spa, ARS_REQ_SHORT);
		/* shouldn't happen, try again later */
		if (rc == -EBUSY)
			break;
		if (rc) {
			set_bit(ARS_FAILED, &nfit_spa->ars_state);
			break;
		}
		clear_bit(ARS_REQ_SHORT, &nfit_spa->ars_state);
		rc = acpi_nfit_query_poison(acpi_desc);
		if (rc)
			break;
		acpi_desc->scrub_spa = nfit_spa;
		ars_complete(acpi_desc, nfit_spa);
		/*
		 * If ars_complete() says we didn't complete the
		 * short scrub, we'll try again with a long
		 * request.
		 */
		acpi_desc->scrub_spa = NULL;
		break;
	case -EBUSY:
	case -ENOMEM:
		/*
		 * BIOS was using ARS, wait for it to complete (or
		 * resources to become available) and then perform our
		 * own scrubs.
		 */
		break;
	default:
		set_bit(ARS_FAILED, &nfit_spa->ars_state);
		break;
	}

	return acpi_nfit_register_region(acpi_desc, nfit_spa);
}

static void ars_complete_all(struct acpi_nfit_desc *acpi_desc)
{
	struct nfit_spa *nfit_spa;

	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		if (test_bit(ARS_FAILED, &nfit_spa->ars_state))
			continue;
		ars_complete(acpi_desc, nfit_spa);
	}
}

static unsigned int __acpi_nfit_scrub(struct acpi_nfit_desc *acpi_desc,
		int query_rc)
{
	unsigned int tmo = acpi_desc->scrub_tmo;
	struct device *dev = acpi_desc->dev;
	struct nfit_spa *nfit_spa;

	lockdep_assert_held(&acpi_desc->init_mutex);

	if (test_bit(ARS_CANCEL, &acpi_desc->scrub_flags))
		return 0;

	if (query_rc == -EBUSY) {
		dev_dbg(dev, "ARS: ARS busy\n");
		return min(30U * 60U, tmo * 2);
	}
	if (query_rc == -ENOSPC) {
		dev_dbg(dev, "ARS: ARS continue\n");
		ars_continue(acpi_desc);
		return 1;
	}
	if (query_rc && query_rc != -EAGAIN) {
		unsigned long long addr, end;

		addr = acpi_desc->ars_status->address;
		end = addr + acpi_desc->ars_status->length;
		dev_dbg(dev, "ARS: %llx-%llx failed (%d)\n", addr, end,
				query_rc);
	}

	ars_complete_all(acpi_desc);
	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		enum nfit_ars_state req_type;
		int rc;

		if (test_bit(ARS_FAILED, &nfit_spa->ars_state))
			continue;

		/* prefer short ARS requests first */
		if (test_bit(ARS_REQ_SHORT, &nfit_spa->ars_state))
			req_type = ARS_REQ_SHORT;
		else if (test_bit(ARS_REQ_LONG, &nfit_spa->ars_state))
			req_type = ARS_REQ_LONG;
		else
			continue;
		rc = ars_start(acpi_desc, nfit_spa, req_type);

		dev = nd_region_dev(nfit_spa->nd_region);
		dev_dbg(dev, "ARS: range %d ARS start %s (%d)\n",
				nfit_spa->spa->range_index,
				req_type == ARS_REQ_SHORT ? "short" : "long",
				rc);
		/*
		 * Hmm, we raced someone else starting ARS? Try again in
		 * a bit.
		 */
		if (rc == -EBUSY)
			return 1;
		if (rc == 0) {
			dev_WARN_ONCE(dev, acpi_desc->scrub_spa,
					"scrub start while range %d active\n",
					acpi_desc->scrub_spa->spa->range_index);
			clear_bit(req_type, &nfit_spa->ars_state);
			acpi_desc->scrub_spa = nfit_spa;
			/*
			 * Consider this spa last for future scrub
			 * requests
			 */
			list_move_tail(&nfit_spa->list, &acpi_desc->spas);
			return 1;
		}

		dev_err(dev, "ARS: range %d ARS failed (%d)\n",
				nfit_spa->spa->range_index, rc);
		set_bit(ARS_FAILED, &nfit_spa->ars_state);
	}
	return 0;
}

static void __sched_ars(struct acpi_nfit_desc *acpi_desc, unsigned int tmo)
{
	lockdep_assert_held(&acpi_desc->init_mutex);

	set_bit(ARS_BUSY, &acpi_desc->scrub_flags);
	/* note this should only be set from within the workqueue */
	if (tmo)
		acpi_desc->scrub_tmo = tmo;
	queue_delayed_work(nfit_wq, &acpi_desc->dwork, tmo * HZ);
}

static void sched_ars(struct acpi_nfit_desc *acpi_desc)
{
	__sched_ars(acpi_desc, 0);
}

static void notify_ars_done(struct acpi_nfit_desc *acpi_desc)
{
	lockdep_assert_held(&acpi_desc->init_mutex);

	clear_bit(ARS_BUSY, &acpi_desc->scrub_flags);
	acpi_desc->scrub_count++;
	if (acpi_desc->scrub_count_state)
		sysfs_notify_dirent(acpi_desc->scrub_count_state);
}

static void acpi_nfit_scrub(struct work_struct *work)
{
	struct acpi_nfit_desc *acpi_desc;
	unsigned int tmo;
	int query_rc;

	acpi_desc = container_of(work, typeof(*acpi_desc), dwork.work);
	mutex_lock(&acpi_desc->init_mutex);
	query_rc = acpi_nfit_query_poison(acpi_desc);
	tmo = __acpi_nfit_scrub(acpi_desc, query_rc);
	if (tmo)
		__sched_ars(acpi_desc, tmo);
	else
		notify_ars_done(acpi_desc);
	memset(acpi_desc->ars_status, 0, acpi_desc->max_ars);
	clear_bit(ARS_POLL, &acpi_desc->scrub_flags);
	mutex_unlock(&acpi_desc->init_mutex);
}

static void acpi_nfit_init_ars(struct acpi_nfit_desc *acpi_desc,
		struct nfit_spa *nfit_spa)
{
	int type = nfit_spa_type(nfit_spa->spa);
	struct nd_cmd_ars_cap ars_cap;
	int rc;

	set_bit(ARS_FAILED, &nfit_spa->ars_state);
	memset(&ars_cap, 0, sizeof(ars_cap));
	rc = ars_get_cap(acpi_desc, &ars_cap, nfit_spa);
	if (rc < 0)
		return;
	/* check that the supported scrub types match the spa type */
	if (type == NFIT_SPA_VOLATILE && ((ars_cap.status >> 16)
				& ND_ARS_VOLATILE) == 0)
		return;
	if (type == NFIT_SPA_PM && ((ars_cap.status >> 16)
				& ND_ARS_PERSISTENT) == 0)
		return;

	nfit_spa->max_ars = ars_cap.max_ars_out;
	nfit_spa->clear_err_unit = ars_cap.clear_err_unit;
	acpi_desc->max_ars = max(nfit_spa->max_ars, acpi_desc->max_ars);
	clear_bit(ARS_FAILED, &nfit_spa->ars_state);
}

static int acpi_nfit_register_regions(struct acpi_nfit_desc *acpi_desc)
{
	struct nfit_spa *nfit_spa;
	int rc, do_sched_ars = 0;

	set_bit(ARS_VALID, &acpi_desc->scrub_flags);
	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		switch (nfit_spa_type(nfit_spa->spa)) {
		case NFIT_SPA_VOLATILE:
		case NFIT_SPA_PM:
			acpi_nfit_init_ars(acpi_desc, nfit_spa);
			break;
		}
	}

	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		switch (nfit_spa_type(nfit_spa->spa)) {
		case NFIT_SPA_VOLATILE:
		case NFIT_SPA_PM:
			/* register regions and kick off initial ARS run */
			rc = ars_register(acpi_desc, nfit_spa);
			if (rc)
				return rc;

			/*
			 * Kick off background ARS if at least one
			 * region successfully registered ARS
			 */
			if (!test_bit(ARS_FAILED, &nfit_spa->ars_state))
				do_sched_ars++;
			break;
		case NFIT_SPA_BDW:
			/* nothing to register */
			break;
		case NFIT_SPA_DCR:
		case NFIT_SPA_VDISK:
		case NFIT_SPA_VCD:
		case NFIT_SPA_PDISK:
		case NFIT_SPA_PCD:
			/* register known regions that don't support ARS */
			rc = acpi_nfit_register_region(acpi_desc, nfit_spa);
			if (rc)
				return rc;
			break;
		default:
			/* don't register unknown regions */
			break;
		}
	}

	if (do_sched_ars)
		sched_ars(acpi_desc);
	return 0;
}

static int acpi_nfit_check_deletions(struct acpi_nfit_desc *acpi_desc,
		struct nfit_table_prev *prev)
{
	struct device *dev = acpi_desc->dev;

	if (!list_empty(&prev->spas) ||
			!list_empty(&prev->memdevs) ||
			!list_empty(&prev->dcrs) ||
			!list_empty(&prev->bdws) ||
			!list_empty(&prev->idts) ||
			!list_empty(&prev->flushes)) {
		dev_err(dev, "new nfit deletes entries (unsupported)\n");
		return -ENXIO;
	}
	return 0;
}

static int acpi_nfit_desc_init_scrub_attr(struct acpi_nfit_desc *acpi_desc)
{
	struct device *dev = acpi_desc->dev;
	struct kernfs_node *nfit;
	struct device *bus_dev;

	if (!ars_supported(acpi_desc->nvdimm_bus))
		return 0;

	bus_dev = to_nvdimm_bus_dev(acpi_desc->nvdimm_bus);
	nfit = sysfs_get_dirent(bus_dev->kobj.sd, "nfit");
	if (!nfit) {
		dev_err(dev, "sysfs_get_dirent 'nfit' failed\n");
		return -ENODEV;
	}
	acpi_desc->scrub_count_state = sysfs_get_dirent(nfit, "scrub");
	sysfs_put(nfit);
	if (!acpi_desc->scrub_count_state) {
		dev_err(dev, "sysfs_get_dirent 'scrub' failed\n");
		return -ENODEV;
	}

	return 0;
}

static void acpi_nfit_unregister(void *data)
{
	struct acpi_nfit_desc *acpi_desc = data;

	nvdimm_bus_unregister(acpi_desc->nvdimm_bus);
}

int acpi_nfit_init(struct acpi_nfit_desc *acpi_desc, void *data, acpi_size sz)
{
	struct device *dev = acpi_desc->dev;
	struct nfit_table_prev prev;
	const void *end;
	int rc;

	if (!acpi_desc->nvdimm_bus) {
		acpi_nfit_init_dsms(acpi_desc);

		acpi_desc->nvdimm_bus = nvdimm_bus_register(dev,
				&acpi_desc->nd_desc);
		if (!acpi_desc->nvdimm_bus)
			return -ENOMEM;

		rc = devm_add_action_or_reset(dev, acpi_nfit_unregister,
				acpi_desc);
		if (rc)
			return rc;

		rc = acpi_nfit_desc_init_scrub_attr(acpi_desc);
		if (rc)
			return rc;

		/* register this acpi_desc for mce notifications */
		mutex_lock(&acpi_desc_lock);
		list_add_tail(&acpi_desc->list, &acpi_descs);
		mutex_unlock(&acpi_desc_lock);
	}

	mutex_lock(&acpi_desc->init_mutex);

	INIT_LIST_HEAD(&prev.spas);
	INIT_LIST_HEAD(&prev.memdevs);
	INIT_LIST_HEAD(&prev.dcrs);
	INIT_LIST_HEAD(&prev.bdws);
	INIT_LIST_HEAD(&prev.idts);
	INIT_LIST_HEAD(&prev.flushes);

	list_cut_position(&prev.spas, &acpi_desc->spas,
				acpi_desc->spas.prev);
	list_cut_position(&prev.memdevs, &acpi_desc->memdevs,
				acpi_desc->memdevs.prev);
	list_cut_position(&prev.dcrs, &acpi_desc->dcrs,
				acpi_desc->dcrs.prev);
	list_cut_position(&prev.bdws, &acpi_desc->bdws,
				acpi_desc->bdws.prev);
	list_cut_position(&prev.idts, &acpi_desc->idts,
				acpi_desc->idts.prev);
	list_cut_position(&prev.flushes, &acpi_desc->flushes,
				acpi_desc->flushes.prev);

	end = data + sz;
	while (!IS_ERR_OR_NULL(data))
		data = add_table(acpi_desc, &prev, data, end);

	if (IS_ERR(data)) {
		dev_dbg(dev, "nfit table parsing error: %ld\n",	PTR_ERR(data));
		rc = PTR_ERR(data);
		goto out_unlock;
	}

	rc = acpi_nfit_check_deletions(acpi_desc, &prev);
	if (rc)
		goto out_unlock;

	rc = nfit_mem_init(acpi_desc);
	if (rc)
		goto out_unlock;

	rc = acpi_nfit_register_dimms(acpi_desc);
	if (rc)
		goto out_unlock;

	rc = acpi_nfit_register_regions(acpi_desc);

 out_unlock:
	mutex_unlock(&acpi_desc->init_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(acpi_nfit_init);

static int acpi_nfit_flush_probe(struct nvdimm_bus_descriptor *nd_desc)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	struct device *dev = acpi_desc->dev;

	/* Bounce the device lock to flush acpi_nfit_add / acpi_nfit_notify */
	nfit_device_lock(dev);
	nfit_device_unlock(dev);

	/* Bounce the init_mutex to complete initial registration */
	mutex_lock(&acpi_desc->init_mutex);
	mutex_unlock(&acpi_desc->init_mutex);

	return 0;
}

static int __acpi_nfit_clear_to_send(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);

	if (nvdimm)
		return 0;
	if (cmd != ND_CMD_ARS_START)
		return 0;

	/*
	 * The kernel and userspace may race to initiate a scrub, but
	 * the scrub thread is prepared to lose that initial race.  It
	 * just needs guarantees that any ARS it initiates are not
	 * interrupted by any intervening start requests from userspace.
	 */
	if (work_busy(&acpi_desc->dwork.work))
		return -EBUSY;

	return 0;
}

/*
 * Prevent security and firmware activate commands from being issued via
 * ioctl.
 */
static int acpi_nfit_clear_to_send(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd, void *buf)
{
	struct nd_cmd_pkg *call_pkg = buf;
	unsigned int func;

	if (nvdimm && cmd == ND_CMD_CALL &&
			call_pkg->nd_family == NVDIMM_FAMILY_INTEL) {
		func = call_pkg->nd_command;
		if (func > NVDIMM_CMD_MAX ||
		    (1 << func) & NVDIMM_INTEL_DENY_CMDMASK)
			return -EOPNOTSUPP;
	}

	/* block all non-nfit bus commands */
	if (!nvdimm && cmd == ND_CMD_CALL &&
			call_pkg->nd_family != NVDIMM_BUS_FAMILY_NFIT)
		return -EOPNOTSUPP;

	return __acpi_nfit_clear_to_send(nd_desc, nvdimm, cmd);
}

int acpi_nfit_ars_rescan(struct acpi_nfit_desc *acpi_desc,
		enum nfit_ars_state req_type)
{
	struct device *dev = acpi_desc->dev;
	int scheduled = 0, busy = 0;
	struct nfit_spa *nfit_spa;

	mutex_lock(&acpi_desc->init_mutex);
	if (test_bit(ARS_CANCEL, &acpi_desc->scrub_flags)) {
		mutex_unlock(&acpi_desc->init_mutex);
		return 0;
	}

	list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
		int type = nfit_spa_type(nfit_spa->spa);

		if (type != NFIT_SPA_PM && type != NFIT_SPA_VOLATILE)
			continue;
		if (test_bit(ARS_FAILED, &nfit_spa->ars_state))
			continue;

		if (test_and_set_bit(req_type, &nfit_spa->ars_state))
			busy++;
		else
			scheduled++;
	}
	if (scheduled) {
		sched_ars(acpi_desc);
		dev_dbg(dev, "ars_scan triggered\n");
	}
	mutex_unlock(&acpi_desc->init_mutex);

	if (scheduled)
		return 0;
	if (busy)
		return -EBUSY;
	return -ENOTTY;
}

void acpi_nfit_desc_init(struct acpi_nfit_desc *acpi_desc, struct device *dev)
{
	struct nvdimm_bus_descriptor *nd_desc;

	dev_set_drvdata(dev, acpi_desc);
	acpi_desc->dev = dev;
	acpi_desc->blk_do_io = acpi_nfit_blk_region_do_io;
	nd_desc = &acpi_desc->nd_desc;
	nd_desc->provider_name = "ACPI.NFIT";
	nd_desc->module = THIS_MODULE;
	nd_desc->ndctl = acpi_nfit_ctl;
	nd_desc->flush_probe = acpi_nfit_flush_probe;
	nd_desc->clear_to_send = acpi_nfit_clear_to_send;
	nd_desc->attr_groups = acpi_nfit_attribute_groups;

	INIT_LIST_HEAD(&acpi_desc->spas);
	INIT_LIST_HEAD(&acpi_desc->dcrs);
	INIT_LIST_HEAD(&acpi_desc->bdws);
	INIT_LIST_HEAD(&acpi_desc->idts);
	INIT_LIST_HEAD(&acpi_desc->flushes);
	INIT_LIST_HEAD(&acpi_desc->memdevs);
	INIT_LIST_HEAD(&acpi_desc->dimms);
	INIT_LIST_HEAD(&acpi_desc->list);
	mutex_init(&acpi_desc->init_mutex);
	acpi_desc->scrub_tmo = 1;
	INIT_DELAYED_WORK(&acpi_desc->dwork, acpi_nfit_scrub);
}
EXPORT_SYMBOL_GPL(acpi_nfit_desc_init);

static void acpi_nfit_put_table(void *table)
{
	acpi_put_table(table);
}

void acpi_nfit_shutdown(void *data)
{
	struct acpi_nfit_desc *acpi_desc = data;
	struct device *bus_dev = to_nvdimm_bus_dev(acpi_desc->nvdimm_bus);

	/*
	 * Destruct under acpi_desc_lock so that nfit_handle_mce does not
	 * race teardown
	 */
	mutex_lock(&acpi_desc_lock);
	list_del(&acpi_desc->list);
	mutex_unlock(&acpi_desc_lock);

	mutex_lock(&acpi_desc->init_mutex);
	set_bit(ARS_CANCEL, &acpi_desc->scrub_flags);
	cancel_delayed_work_sync(&acpi_desc->dwork);
	mutex_unlock(&acpi_desc->init_mutex);

	/*
	 * Bounce the nvdimm bus lock to make sure any in-flight
	 * acpi_nfit_ars_rescan() submissions have had a chance to
	 * either submit or see ->cancel set.
	 */
	nfit_device_lock(bus_dev);
	nfit_device_unlock(bus_dev);

	flush_workqueue(nfit_wq);
}
EXPORT_SYMBOL_GPL(acpi_nfit_shutdown);

static int acpi_nfit_add(struct acpi_device *adev)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_nfit_desc *acpi_desc;
	struct device *dev = &adev->dev;
	struct acpi_table_header *tbl;
	acpi_status status = AE_OK;
	acpi_size sz;
	int rc = 0;

	status = acpi_get_table(ACPI_SIG_NFIT, 0, &tbl);
	if (ACPI_FAILURE(status)) {
		/* The NVDIMM root device allows OS to trigger enumeration of
		 * NVDIMMs through NFIT at boot time and re-enumeration at
		 * root level via the _FIT method during runtime.
		 * This is ok to return 0 here, we could have an nvdimm
		 * hotplugged later and evaluate _FIT method which returns
		 * data in the format of a series of NFIT Structures.
		 */
		dev_dbg(dev, "failed to find NFIT at startup\n");
		return 0;
	}

	rc = devm_add_action_or_reset(dev, acpi_nfit_put_table, tbl);
	if (rc)
		return rc;
	sz = tbl->length;

	acpi_desc = devm_kzalloc(dev, sizeof(*acpi_desc), GFP_KERNEL);
	if (!acpi_desc)
		return -ENOMEM;
	acpi_nfit_desc_init(acpi_desc, &adev->dev);

	/* Save the acpi header for exporting the revision via sysfs */
	acpi_desc->acpi_header = *tbl;

	/* Evaluate _FIT and override with that if present */
	status = acpi_evaluate_object(adev->handle, "_FIT", NULL, &buf);
	if (ACPI_SUCCESS(status) && buf.length > 0) {
		union acpi_object *obj = buf.pointer;

		if (obj->type == ACPI_TYPE_BUFFER)
			rc = acpi_nfit_init(acpi_desc, obj->buffer.pointer,
					obj->buffer.length);
		else
			dev_dbg(dev, "invalid type %d, ignoring _FIT\n",
				(int) obj->type);
		kfree(buf.pointer);
	} else
		/* skip over the lead-in header table */
		rc = acpi_nfit_init(acpi_desc, (void *) tbl
				+ sizeof(struct acpi_table_nfit),
				sz - sizeof(struct acpi_table_nfit));

	if (rc)
		return rc;
	return devm_add_action_or_reset(dev, acpi_nfit_shutdown, acpi_desc);
}

static int acpi_nfit_remove(struct acpi_device *adev)
{
	/* see acpi_nfit_unregister */
	return 0;
}

static void acpi_nfit_update_notify(struct device *dev, acpi_handle handle)
{
	struct acpi_nfit_desc *acpi_desc = dev_get_drvdata(dev);
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int ret;

	if (!dev->driver) {
		/* dev->driver may be null if we're being removed */
		dev_dbg(dev, "no driver found for dev\n");
		return;
	}

	if (!acpi_desc) {
		acpi_desc = devm_kzalloc(dev, sizeof(*acpi_desc), GFP_KERNEL);
		if (!acpi_desc)
			return;
		acpi_nfit_desc_init(acpi_desc, dev);
	} else {
		/*
		 * Finish previous registration before considering new
		 * regions.
		 */
		flush_workqueue(nfit_wq);
	}

	/* Evaluate _FIT */
	status = acpi_evaluate_object(handle, "_FIT", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "failed to evaluate _FIT\n");
		return;
	}

	obj = buf.pointer;
	if (obj->type == ACPI_TYPE_BUFFER) {
		ret = acpi_nfit_init(acpi_desc, obj->buffer.pointer,
				obj->buffer.length);
		if (ret)
			dev_err(dev, "failed to merge updated NFIT\n");
	} else
		dev_err(dev, "Invalid _FIT\n");
	kfree(buf.pointer);
}

static void acpi_nfit_uc_error_notify(struct device *dev, acpi_handle handle)
{
	struct acpi_nfit_desc *acpi_desc = dev_get_drvdata(dev);

	if (acpi_desc->scrub_mode == HW_ERROR_SCRUB_ON)
		acpi_nfit_ars_rescan(acpi_desc, ARS_REQ_LONG);
	else
		acpi_nfit_ars_rescan(acpi_desc, ARS_REQ_SHORT);
}

void __acpi_nfit_notify(struct device *dev, acpi_handle handle, u32 event)
{
	dev_dbg(dev, "event: 0x%x\n", event);

	switch (event) {
	case NFIT_NOTIFY_UPDATE:
		return acpi_nfit_update_notify(dev, handle);
	case NFIT_NOTIFY_UC_MEMORY_ERROR:
		return acpi_nfit_uc_error_notify(dev, handle);
	default:
		return;
	}
}
EXPORT_SYMBOL_GPL(__acpi_nfit_notify);

static void acpi_nfit_notify(struct acpi_device *adev, u32 event)
{
	nfit_device_lock(&adev->dev);
	__acpi_nfit_notify(&adev->dev, adev->handle, event);
	nfit_device_unlock(&adev->dev);
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
		.notify = acpi_nfit_notify,
	},
};

static __init int nfit_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct acpi_table_nfit) != 40);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_system_address) != 64);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_memory_map) != 48);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_interleave) != 20);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_smbios) != 9);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_control_region) != 80);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_data_region) != 40);
	BUILD_BUG_ON(sizeof(struct acpi_nfit_capabilities) != 16);

	guid_parse(UUID_VOLATILE_MEMORY, &nfit_uuid[NFIT_SPA_VOLATILE]);
	guid_parse(UUID_PERSISTENT_MEMORY, &nfit_uuid[NFIT_SPA_PM]);
	guid_parse(UUID_CONTROL_REGION, &nfit_uuid[NFIT_SPA_DCR]);
	guid_parse(UUID_DATA_REGION, &nfit_uuid[NFIT_SPA_BDW]);
	guid_parse(UUID_VOLATILE_VIRTUAL_DISK, &nfit_uuid[NFIT_SPA_VDISK]);
	guid_parse(UUID_VOLATILE_VIRTUAL_CD, &nfit_uuid[NFIT_SPA_VCD]);
	guid_parse(UUID_PERSISTENT_VIRTUAL_DISK, &nfit_uuid[NFIT_SPA_PDISK]);
	guid_parse(UUID_PERSISTENT_VIRTUAL_CD, &nfit_uuid[NFIT_SPA_PCD]);
	guid_parse(UUID_NFIT_BUS, &nfit_uuid[NFIT_DEV_BUS]);
	guid_parse(UUID_NFIT_DIMM, &nfit_uuid[NFIT_DEV_DIMM]);
	guid_parse(UUID_NFIT_DIMM_N_HPE1, &nfit_uuid[NFIT_DEV_DIMM_N_HPE1]);
	guid_parse(UUID_NFIT_DIMM_N_HPE2, &nfit_uuid[NFIT_DEV_DIMM_N_HPE2]);
	guid_parse(UUID_NFIT_DIMM_N_MSFT, &nfit_uuid[NFIT_DEV_DIMM_N_MSFT]);
	guid_parse(UUID_NFIT_DIMM_N_HYPERV, &nfit_uuid[NFIT_DEV_DIMM_N_HYPERV]);
	guid_parse(UUID_INTEL_BUS, &nfit_uuid[NFIT_BUS_INTEL]);

	nfit_wq = create_singlethread_workqueue("nfit");
	if (!nfit_wq)
		return -ENOMEM;

	nfit_mce_register();
	ret = acpi_bus_register_driver(&acpi_nfit_driver);
	if (ret) {
		nfit_mce_unregister();
		destroy_workqueue(nfit_wq);
	}

	return ret;

}

static __exit void nfit_exit(void)
{
	nfit_mce_unregister();
	acpi_bus_unregister_driver(&acpi_nfit_driver);
	destroy_workqueue(nfit_wq);
	WARN_ON(!list_empty(&acpi_descs));
}

module_init(nfit_init);
module_exit(nfit_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
