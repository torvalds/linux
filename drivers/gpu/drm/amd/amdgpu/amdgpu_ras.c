/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>
#include <linux/pm_runtime.h>

#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_xgmi.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"
#include "atom.h"

static const char *RAS_FS_NAME = "ras";

const char *ras_error_string[] = {
	"none",
	"parity",
	"single_correctable",
	"multi_uncorrectable",
	"poison",
};

const char *ras_block_string[] = {
	"umc",
	"sdma",
	"gfx",
	"mmhub",
	"athub",
	"pcie_bif",
	"hdp",
	"xgmi_wafl",
	"df",
	"smn",
	"sem",
	"mp0",
	"mp1",
	"fuse",
};

#define ras_err_str(i) (ras_error_string[ffs(i)])
#define ras_block_str(i) (ras_block_string[i])

#define RAS_DEFAULT_FLAGS (AMDGPU_RAS_FLAG_INIT_BY_VBIOS)

/* inject address is 52 bits */
#define	RAS_UMC_INJECT_ADDR_LIMIT	(0x1ULL << 52)

/* typical ECC bad page rate(1 bad page per 100MB VRAM) */
#define RAS_BAD_PAGE_RATE		(100 * 1024 * 1024ULL)

enum amdgpu_ras_retire_page_reservation {
	AMDGPU_RAS_RETIRE_PAGE_RESERVED,
	AMDGPU_RAS_RETIRE_PAGE_PENDING,
	AMDGPU_RAS_RETIRE_PAGE_FAULT,
};

atomic_t amdgpu_ras_in_intr = ATOMIC_INIT(0);

static bool amdgpu_ras_check_bad_page_unlock(struct amdgpu_ras *con,
				uint64_t addr);
static bool amdgpu_ras_check_bad_page(struct amdgpu_device *adev,
				uint64_t addr);

void amdgpu_ras_set_error_query_ready(struct amdgpu_device *adev, bool ready)
{
	if (adev && amdgpu_ras_get_context(adev))
		amdgpu_ras_get_context(adev)->error_query_ready = ready;
}

static bool amdgpu_ras_get_error_query_ready(struct amdgpu_device *adev)
{
	if (adev && amdgpu_ras_get_context(adev))
		return amdgpu_ras_get_context(adev)->error_query_ready;

	return false;
}

static int amdgpu_reserve_page_direct(struct amdgpu_device *adev, uint64_t address)
{
	struct ras_err_data err_data = {0, 0, 0, NULL};
	struct eeprom_table_record err_rec;

	if ((address >= adev->gmc.mc_vram_size) ||
	    (address >= RAS_UMC_INJECT_ADDR_LIMIT)) {
		dev_warn(adev->dev,
		         "RAS WARN: input address 0x%llx is invalid.\n",
		         address);
		return -EINVAL;
	}

	if (amdgpu_ras_check_bad_page(adev, address)) {
		dev_warn(adev->dev,
			 "RAS WARN: 0x%llx has already been marked as bad page!\n",
			 address);
		return 0;
	}

	memset(&err_rec, 0x0, sizeof(struct eeprom_table_record));

	err_rec.address = address;
	err_rec.retired_page = address >> AMDGPU_GPU_PAGE_SHIFT;
	err_rec.ts = (uint64_t)ktime_get_real_seconds();
	err_rec.err_type = AMDGPU_RAS_EEPROM_ERR_NON_RECOVERABLE;

	err_data.err_addr = &err_rec;
	err_data.err_addr_cnt = 1;

	if (amdgpu_bad_page_threshold != 0) {
		amdgpu_ras_add_bad_pages(adev, err_data.err_addr,
					 err_data.err_addr_cnt);
		amdgpu_ras_save_bad_pages(adev);
	}

	dev_warn(adev->dev, "WARNING: THIS IS ONLY FOR TEST PURPOSES AND WILL CORRUPT RAS EEPROM\n");
	dev_warn(adev->dev, "Clear EEPROM:\n");
	dev_warn(adev->dev, "    echo 1 > /sys/kernel/debug/dri/0/ras/ras_eeprom_reset\n");

	return 0;
}

static ssize_t amdgpu_ras_debugfs_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct ras_manager *obj = (struct ras_manager *)file_inode(f)->i_private;
	struct ras_query_if info = {
		.head = obj->head,
	};
	ssize_t s;
	char val[128];

	if (amdgpu_ras_query_error_status(obj->adev, &info))
		return -EINVAL;

	s = snprintf(val, sizeof(val), "%s: %lu\n%s: %lu\n",
			"ue", info.ue_count,
			"ce", info.ce_count);
	if (*pos >= s)
		return 0;

	s -= *pos;
	s = min_t(u64, s, size);


	if (copy_to_user(buf, &val[*pos], s))
		return -EINVAL;

	*pos += s;

	return s;
}

static const struct file_operations amdgpu_ras_debugfs_ops = {
	.owner = THIS_MODULE,
	.read = amdgpu_ras_debugfs_read,
	.write = NULL,
	.llseek = default_llseek
};

static int amdgpu_ras_find_block_id_by_name(const char *name, int *block_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ras_block_string); i++) {
		*block_id = i;
		if (strcmp(name, ras_block_str(i)) == 0)
			return 0;
	}
	return -EINVAL;
}

static int amdgpu_ras_debugfs_ctrl_parse_data(struct file *f,
		const char __user *buf, size_t size,
		loff_t *pos, struct ras_debug_if *data)
{
	ssize_t s = min_t(u64, 64, size);
	char str[65];
	char block_name[33];
	char err[9] = "ue";
	int op = -1;
	int block_id;
	uint32_t sub_block;
	u64 address, value;

	if (*pos)
		return -EINVAL;
	*pos = size;

	memset(str, 0, sizeof(str));
	memset(data, 0, sizeof(*data));

	if (copy_from_user(str, buf, s))
		return -EINVAL;

	if (sscanf(str, "disable %32s", block_name) == 1)
		op = 0;
	else if (sscanf(str, "enable %32s %8s", block_name, err) == 2)
		op = 1;
	else if (sscanf(str, "inject %32s %8s", block_name, err) == 2)
		op = 2;
	else if (strstr(str, "retire_page") != NULL)
		op = 3;
	else if (str[0] && str[1] && str[2] && str[3])
		/* ascii string, but commands are not matched. */
		return -EINVAL;

	if (op != -1) {
		if (op == 3) {
			if (sscanf(str, "%*s 0x%llx", &address) != 1 &&
			    sscanf(str, "%*s %llu", &address) != 1)
				return -EINVAL;

			data->op = op;
			data->inject.address = address;

			return 0;
		}

		if (amdgpu_ras_find_block_id_by_name(block_name, &block_id))
			return -EINVAL;

		data->head.block = block_id;
		/* only ue and ce errors are supported */
		if (!memcmp("ue", err, 2))
			data->head.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
		else if (!memcmp("ce", err, 2))
			data->head.type = AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE;
		else
			return -EINVAL;

		data->op = op;

		if (op == 2) {
			if (sscanf(str, "%*s %*s %*s 0x%x 0x%llx 0x%llx",
				   &sub_block, &address, &value) != 3 &&
			    sscanf(str, "%*s %*s %*s %u %llu %llu",
				   &sub_block, &address, &value) != 3)
				return -EINVAL;
			data->head.sub_block_index = sub_block;
			data->inject.address = address;
			data->inject.value = value;
		}
	} else {
		if (size < sizeof(*data))
			return -EINVAL;

		if (copy_from_user(data, buf, sizeof(*data)))
			return -EINVAL;
	}

	return 0;
}

/**
 * DOC: AMDGPU RAS debugfs control interface
 *
 * The control interface accepts struct ras_debug_if which has two members.
 *
 * First member: ras_debug_if::head or ras_debug_if::inject.
 *
 * head is used to indicate which IP block will be under control.
 *
 * head has four members, they are block, type, sub_block_index, name.
 * block: which IP will be under control.
 * type: what kind of error will be enabled/disabled/injected.
 * sub_block_index: some IPs have subcomponets. say, GFX, sDMA.
 * name: the name of IP.
 *
 * inject has two more members than head, they are address, value.
 * As their names indicate, inject operation will write the
 * value to the address.
 *
 * The second member: struct ras_debug_if::op.
 * It has three kinds of operations.
 *
 * - 0: disable RAS on the block. Take ::head as its data.
 * - 1: enable RAS on the block. Take ::head as its data.
 * - 2: inject errors on the block. Take ::inject as its data.
 *
 * How to use the interface?
 *
 * In a program
 *
 * Copy the struct ras_debug_if in your code and initialize it.
 * Write the struct to the control interface.
 *
 * From shell
 *
 * .. code-block:: bash
 *
 *	echo "disable <block>" > /sys/kernel/debug/dri/<N>/ras/ras_ctrl
 *	echo "enable  <block> <error>" > /sys/kernel/debug/dri/<N>/ras/ras_ctrl
 *	echo "inject  <block> <error> <sub-block> <address> <value> > /sys/kernel/debug/dri/<N>/ras/ras_ctrl
 *
 * Where N, is the card which you want to affect.
 *
 * "disable" requires only the block.
 * "enable" requires the block and error type.
 * "inject" requires the block, error type, address, and value.
 *
 * The block is one of: umc, sdma, gfx, etc.
 *	see ras_block_string[] for details
 *
 * The error type is one of: ue, ce, where,
 *	ue is multi-uncorrectable
 *	ce is single-correctable
 *
 * The sub-block is a the sub-block index, pass 0 if there is no sub-block.
 * The address and value are hexadecimal numbers, leading 0x is optional.
 *
 * For instance,
 *
 * .. code-block:: bash
 *
 *	echo inject umc ue 0x0 0x0 0x0 > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *	echo inject umc ce 0 0 0 > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *	echo disable umc > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *
 * How to check the result of the operation?
 *
 * To check disable/enable, see "ras" features at,
 * /sys/class/drm/card[0/1/2...]/device/ras/features
 *
 * To check inject, see the corresponding error count at,
 * /sys/class/drm/card[0/1/2...]/device/ras/[gfx|sdma|umc|...]_err_count
 *
 * .. note::
 *	Operations are only allowed on blocks which are supported.
 *	Check the "ras" mask at /sys/module/amdgpu/parameters/ras_mask
 *	to see which blocks support RAS on a particular asic.
 *
 */
static ssize_t amdgpu_ras_debugfs_ctrl_write(struct file *f, const char __user *buf,
		size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct ras_debug_if data;
	int ret = 0;

	if (!amdgpu_ras_get_error_query_ready(adev)) {
		dev_warn(adev->dev, "RAS WARN: error injection "
				"currently inaccessible\n");
		return size;
	}

	ret = amdgpu_ras_debugfs_ctrl_parse_data(f, buf, size, pos, &data);
	if (ret)
		return -EINVAL;

	if (data.op == 3) {
		ret = amdgpu_reserve_page_direct(adev, data.inject.address);
		if (!ret)
			return size;
		else
			return ret;
	}

	if (!amdgpu_ras_is_supported(adev, data.head.block))
		return -EINVAL;

	switch (data.op) {
	case 0:
		ret = amdgpu_ras_feature_enable(adev, &data.head, 0);
		break;
	case 1:
		ret = amdgpu_ras_feature_enable(adev, &data.head, 1);
		break;
	case 2:
		if ((data.inject.address >= adev->gmc.mc_vram_size) ||
		    (data.inject.address >= RAS_UMC_INJECT_ADDR_LIMIT)) {
			dev_warn(adev->dev, "RAS WARN: input address "
					"0x%llx is invalid.",
					data.inject.address);
			ret = -EINVAL;
			break;
		}

		/* umc ce/ue error injection for a bad page is not allowed */
		if ((data.head.block == AMDGPU_RAS_BLOCK__UMC) &&
		    amdgpu_ras_check_bad_page(adev, data.inject.address)) {
			dev_warn(adev->dev, "RAS WARN: 0x%llx has been marked "
					"as bad before error injection!\n",
					data.inject.address);
			break;
		}

		/* data.inject.address is offset instead of absolute gpu address */
		ret = amdgpu_ras_error_inject(adev, &data.inject);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return -EINVAL;

	return size;
}

/**
 * DOC: AMDGPU RAS debugfs EEPROM table reset interface
 *
 * Some boards contain an EEPROM which is used to persistently store a list of
 * bad pages which experiences ECC errors in vram.  This interface provides
 * a way to reset the EEPROM, e.g., after testing error injection.
 *
 * Usage:
 *
 * .. code-block:: bash
 *
 *	echo 1 > ../ras/ras_eeprom_reset
 *
 * will reset EEPROM table to 0 entries.
 *
 */
static ssize_t amdgpu_ras_debugfs_eeprom_write(struct file *f, const char __user *buf,
		size_t size, loff_t *pos)
{
	struct amdgpu_device *adev =
		(struct amdgpu_device *)file_inode(f)->i_private;
	int ret;

	ret = amdgpu_ras_eeprom_reset_table(
			&(amdgpu_ras_get_context(adev)->eeprom_control));

	if (ret == 1) {
		amdgpu_ras_get_context(adev)->flags = RAS_DEFAULT_FLAGS;
		return size;
	} else {
		return -EIO;
	}
}

static const struct file_operations amdgpu_ras_debugfs_ctrl_ops = {
	.owner = THIS_MODULE,
	.read = NULL,
	.write = amdgpu_ras_debugfs_ctrl_write,
	.llseek = default_llseek
};

static const struct file_operations amdgpu_ras_debugfs_eeprom_ops = {
	.owner = THIS_MODULE,
	.read = NULL,
	.write = amdgpu_ras_debugfs_eeprom_write,
	.llseek = default_llseek
};

/**
 * DOC: AMDGPU RAS sysfs Error Count Interface
 *
 * It allows the user to read the error count for each IP block on the gpu through
 * /sys/class/drm/card[0/1/2...]/device/ras/[gfx/sdma/...]_err_count
 *
 * It outputs the multiple lines which report the uncorrected (ue) and corrected
 * (ce) error counts.
 *
 * The format of one line is below,
 *
 * [ce|ue]: count
 *
 * Example:
 *
 * .. code-block:: bash
 *
 *	ue: 0
 *	ce: 1
 *
 */
static ssize_t amdgpu_ras_sysfs_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ras_manager *obj = container_of(attr, struct ras_manager, sysfs_attr);
	struct ras_query_if info = {
		.head = obj->head,
	};

	if (!amdgpu_ras_get_error_query_ready(obj->adev))
		return sysfs_emit(buf, "Query currently inaccessible\n");

	if (amdgpu_ras_query_error_status(obj->adev, &info))
		return -EINVAL;


	if (obj->adev->asic_type == CHIP_ALDEBARAN) {
		if (amdgpu_ras_reset_error_status(obj->adev, info.head.block))
			DRM_WARN("Failed to reset error counter and error status");
	}

	return sysfs_emit(buf, "%s: %lu\n%s: %lu\n", "ue", info.ue_count,
			  "ce", info.ce_count);
}

/* obj begin */

#define get_obj(obj) do { (obj)->use++; } while (0)
#define alive_obj(obj) ((obj)->use)

static inline void put_obj(struct ras_manager *obj)
{
	if (obj && (--obj->use == 0))
		list_del(&obj->node);
	if (obj && (obj->use < 0))
		DRM_ERROR("RAS ERROR: Unbalance obj(%s) use\n", obj->head.name);
}

/* make one obj and return it. */
static struct ras_manager *amdgpu_ras_create_obj(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;

	if (!adev->ras_enabled || !con)
		return NULL;

	if (head->block >= AMDGPU_RAS_BLOCK_COUNT)
		return NULL;

	obj = &con->objs[head->block];
	/* already exist. return obj? */
	if (alive_obj(obj))
		return NULL;

	obj->head = *head;
	obj->adev = adev;
	list_add(&obj->node, &con->head);
	get_obj(obj);

	return obj;
}

/* return an obj equal to head, or the first when head is NULL */
struct ras_manager *amdgpu_ras_find_obj(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;
	int i;

	if (!adev->ras_enabled || !con)
		return NULL;

	if (head) {
		if (head->block >= AMDGPU_RAS_BLOCK_COUNT)
			return NULL;

		obj = &con->objs[head->block];

		if (alive_obj(obj)) {
			WARN_ON(head->block != obj->head.block);
			return obj;
		}
	} else {
		for (i = 0; i < AMDGPU_RAS_BLOCK_COUNT; i++) {
			obj = &con->objs[i];
			if (alive_obj(obj)) {
				WARN_ON(i != obj->head.block);
				return obj;
			}
		}
	}

	return NULL;
}
/* obj end */

/* feature ctl begin */
static int amdgpu_ras_is_feature_allowed(struct amdgpu_device *adev,
					 struct ras_common_if *head)
{
	return adev->ras_hw_enabled & BIT(head->block);
}

static int amdgpu_ras_is_feature_enabled(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	return con->features & BIT(head->block);
}

/*
 * if obj is not created, then create one.
 * set feature enable flag.
 */
static int __amdgpu_ras_feature_enable(struct amdgpu_device *adev,
		struct ras_common_if *head, int enable)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, head);

	/* If hardware does not support ras, then do not create obj.
	 * But if hardware support ras, we can create the obj.
	 * Ras framework checks con->hw_supported to see if it need do
	 * corresponding initialization.
	 * IP checks con->support to see if it need disable ras.
	 */
	if (!amdgpu_ras_is_feature_allowed(adev, head))
		return 0;
	if (!(!!enable ^ !!amdgpu_ras_is_feature_enabled(adev, head)))
		return 0;

	if (enable) {
		if (!obj) {
			obj = amdgpu_ras_create_obj(adev, head);
			if (!obj)
				return -EINVAL;
		} else {
			/* In case we create obj somewhere else */
			get_obj(obj);
		}
		con->features |= BIT(head->block);
	} else {
		if (obj && amdgpu_ras_is_feature_enabled(adev, head)) {
			con->features &= ~BIT(head->block);
			put_obj(obj);
		}
	}

	return 0;
}

/* wrapper of psp_ras_enable_features */
int amdgpu_ras_feature_enable(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	union ta_ras_cmd_input *info;
	int ret;

	if (!con)
		return -EINVAL;

	info = kzalloc(sizeof(union ta_ras_cmd_input), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!enable) {
		info->disable_features = (struct ta_ras_disable_features_input) {
			.block_id =  amdgpu_ras_block_to_ta(head->block),
			.error_type = amdgpu_ras_error_to_ta(head->type),
		};
	} else {
		info->enable_features = (struct ta_ras_enable_features_input) {
			.block_id =  amdgpu_ras_block_to_ta(head->block),
			.error_type = amdgpu_ras_error_to_ta(head->type),
		};
	}

	/* Do not enable if it is not allowed. */
	WARN_ON(enable && !amdgpu_ras_is_feature_allowed(adev, head));
	/* Are we alerady in that state we are going to set? */
	if (!(!!enable ^ !!amdgpu_ras_is_feature_enabled(adev, head))) {
		ret = 0;
		goto out;
	}

	if (!amdgpu_ras_intr_triggered()) {
		ret = psp_ras_enable_features(&adev->psp, info, enable);
		if (ret) {
			dev_err(adev->dev, "ras %s %s failed %d\n",
				enable ? "enable":"disable",
				ras_block_str(head->block),
				ret);
			goto out;
		}
	}

	/* setup the obj */
	__amdgpu_ras_feature_enable(adev, head, enable);
	ret = 0;
out:
	kfree(info);
	return ret;
}

/* Only used in device probe stage and called only once. */
int amdgpu_ras_feature_enable_on_boot(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int ret;

	if (!con)
		return -EINVAL;

	if (con->flags & AMDGPU_RAS_FLAG_INIT_BY_VBIOS) {
		if (enable) {
			/* There is no harm to issue a ras TA cmd regardless of
			 * the currecnt ras state.
			 * If current state == target state, it will do nothing
			 * But sometimes it requests driver to reset and repost
			 * with error code -EAGAIN.
			 */
			ret = amdgpu_ras_feature_enable(adev, head, 1);
			/* With old ras TA, we might fail to enable ras.
			 * Log it and just setup the object.
			 * TODO need remove this WA in the future.
			 */
			if (ret == -EINVAL) {
				ret = __amdgpu_ras_feature_enable(adev, head, 1);
				if (!ret)
					dev_info(adev->dev,
						"RAS INFO: %s setup object\n",
						ras_block_str(head->block));
			}
		} else {
			/* setup the object then issue a ras TA disable cmd.*/
			ret = __amdgpu_ras_feature_enable(adev, head, 1);
			if (ret)
				return ret;

			/* gfx block ras dsiable cmd must send to ras-ta */
			if (head->block == AMDGPU_RAS_BLOCK__GFX)
				con->features |= BIT(head->block);

			ret = amdgpu_ras_feature_enable(adev, head, 0);

			/* clean gfx block ras features flag */
			if (adev->ras_enabled && head->block == AMDGPU_RAS_BLOCK__GFX)
				con->features &= ~BIT(head->block);
		}
	} else
		ret = amdgpu_ras_feature_enable(adev, head, enable);

	return ret;
}

static int amdgpu_ras_disable_all_features(struct amdgpu_device *adev,
		bool bypass)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &con->head, node) {
		/* bypass psp.
		 * aka just release the obj and corresponding flags
		 */
		if (bypass) {
			if (__amdgpu_ras_feature_enable(adev, &obj->head, 0))
				break;
		} else {
			if (amdgpu_ras_feature_enable(adev, &obj->head, 0))
				break;
		}
	}

	return con->features;
}

static int amdgpu_ras_enable_all_features(struct amdgpu_device *adev,
		bool bypass)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int ras_block_count = AMDGPU_RAS_BLOCK_COUNT;
	int i;
	const enum amdgpu_ras_error_type default_ras_type =
		AMDGPU_RAS_ERROR__NONE;

	for (i = 0; i < ras_block_count; i++) {
		struct ras_common_if head = {
			.block = i,
			.type = default_ras_type,
			.sub_block_index = 0,
		};
		strcpy(head.name, ras_block_str(i));
		if (bypass) {
			/*
			 * bypass psp. vbios enable ras for us.
			 * so just create the obj
			 */
			if (__amdgpu_ras_feature_enable(adev, &head, 1))
				break;
		} else {
			if (amdgpu_ras_feature_enable(adev, &head, 1))
				break;
		}
	}

	return con->features;
}
/* feature ctl end */

/* query/inject/cure begin */
int amdgpu_ras_query_error_status(struct amdgpu_device *adev,
				  struct ras_query_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ras_err_data err_data = {0, 0, 0, NULL};
	int i;

	if (!obj)
		return -EINVAL;

	switch (info->head.block) {
	case AMDGPU_RAS_BLOCK__UMC:
		if (adev->umc.ras_funcs &&
		    adev->umc.ras_funcs->query_ras_error_count)
			adev->umc.ras_funcs->query_ras_error_count(adev, &err_data);
		/* umc query_ras_error_address is also responsible for clearing
		 * error status
		 */
		if (adev->umc.ras_funcs &&
		    adev->umc.ras_funcs->query_ras_error_address)
			adev->umc.ras_funcs->query_ras_error_address(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__SDMA:
		if (adev->sdma.funcs->query_ras_error_count) {
			for (i = 0; i < adev->sdma.num_instances; i++)
				adev->sdma.funcs->query_ras_error_count(adev, i,
									&err_data);
		}
		break;
	case AMDGPU_RAS_BLOCK__GFX:
		if (adev->gfx.ras_funcs &&
		    adev->gfx.ras_funcs->query_ras_error_count)
			adev->gfx.ras_funcs->query_ras_error_count(adev, &err_data);

		if (adev->gfx.ras_funcs &&
		    adev->gfx.ras_funcs->query_ras_error_status)
			adev->gfx.ras_funcs->query_ras_error_status(adev);
		break;
	case AMDGPU_RAS_BLOCK__MMHUB:
		if (adev->mmhub.ras_funcs &&
		    adev->mmhub.ras_funcs->query_ras_error_count)
			adev->mmhub.ras_funcs->query_ras_error_count(adev, &err_data);

		if (adev->mmhub.ras_funcs &&
		    adev->mmhub.ras_funcs->query_ras_error_status)
			adev->mmhub.ras_funcs->query_ras_error_status(adev);
		break;
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		if (adev->nbio.ras_funcs &&
		    adev->nbio.ras_funcs->query_ras_error_count)
			adev->nbio.ras_funcs->query_ras_error_count(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
		if (adev->gmc.xgmi.ras_funcs &&
		    adev->gmc.xgmi.ras_funcs->query_ras_error_count)
			adev->gmc.xgmi.ras_funcs->query_ras_error_count(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__HDP:
		if (adev->hdp.ras_funcs &&
		    adev->hdp.ras_funcs->query_ras_error_count)
			adev->hdp.ras_funcs->query_ras_error_count(adev, &err_data);
		break;
	default:
		break;
	}

	obj->err_data.ue_count += err_data.ue_count;
	obj->err_data.ce_count += err_data.ce_count;

	info->ue_count = obj->err_data.ue_count;
	info->ce_count = obj->err_data.ce_count;

	if (err_data.ce_count) {
		if (adev->smuio.funcs &&
		    adev->smuio.funcs->get_socket_id &&
		    adev->smuio.funcs->get_die_id) {
			dev_info(adev->dev, "socket: %d, die: %d "
					"%ld correctable hardware errors "
					"detected in %s block, no user "
					"action is needed.\n",
					adev->smuio.funcs->get_socket_id(adev),
					adev->smuio.funcs->get_die_id(adev),
					obj->err_data.ce_count,
					ras_block_str(info->head.block));
		} else {
			dev_info(adev->dev, "%ld correctable hardware errors "
					"detected in %s block, no user "
					"action is needed.\n",
					obj->err_data.ce_count,
					ras_block_str(info->head.block));
		}
	}
	if (err_data.ue_count) {
		if (adev->smuio.funcs &&
		    adev->smuio.funcs->get_socket_id &&
		    adev->smuio.funcs->get_die_id) {
			dev_info(adev->dev, "socket: %d, die: %d "
					"%ld uncorrectable hardware errors "
					"detected in %s block\n",
					adev->smuio.funcs->get_socket_id(adev),
					adev->smuio.funcs->get_die_id(adev),
					obj->err_data.ue_count,
					ras_block_str(info->head.block));
		} else {
			dev_info(adev->dev, "%ld uncorrectable hardware errors "
					"detected in %s block\n",
					obj->err_data.ue_count,
					ras_block_str(info->head.block));
		}
	}

	return 0;
}

int amdgpu_ras_reset_error_status(struct amdgpu_device *adev,
		enum amdgpu_ras_block block)
{
	if (!amdgpu_ras_is_supported(adev, block))
		return -EINVAL;

	switch (block) {
	case AMDGPU_RAS_BLOCK__GFX:
		if (adev->gfx.ras_funcs &&
		    adev->gfx.ras_funcs->reset_ras_error_count)
			adev->gfx.ras_funcs->reset_ras_error_count(adev);

		if (adev->gfx.ras_funcs &&
		    adev->gfx.ras_funcs->reset_ras_error_status)
			adev->gfx.ras_funcs->reset_ras_error_status(adev);
		break;
	case AMDGPU_RAS_BLOCK__MMHUB:
		if (adev->mmhub.ras_funcs &&
		    adev->mmhub.ras_funcs->reset_ras_error_count)
			adev->mmhub.ras_funcs->reset_ras_error_count(adev);

		if (adev->mmhub.ras_funcs &&
		    adev->mmhub.ras_funcs->reset_ras_error_status)
			adev->mmhub.ras_funcs->reset_ras_error_status(adev);
		break;
	case AMDGPU_RAS_BLOCK__SDMA:
		if (adev->sdma.funcs->reset_ras_error_count)
			adev->sdma.funcs->reset_ras_error_count(adev);
		break;
	case AMDGPU_RAS_BLOCK__HDP:
		if (adev->hdp.ras_funcs &&
		    adev->hdp.ras_funcs->reset_ras_error_count)
			adev->hdp.ras_funcs->reset_ras_error_count(adev);
		break;
	default:
		break;
	}

	return 0;
}

/* Trigger XGMI/WAFL error */
static int amdgpu_ras_error_inject_xgmi(struct amdgpu_device *adev,
				 struct ta_ras_trigger_error_input *block_info)
{
	int ret;

	if (amdgpu_dpm_set_df_cstate(adev, DF_CSTATE_DISALLOW))
		dev_warn(adev->dev, "Failed to disallow df cstate");

	if (amdgpu_dpm_allow_xgmi_power_down(adev, false))
		dev_warn(adev->dev, "Failed to disallow XGMI power down");

	ret = psp_ras_trigger_error(&adev->psp, block_info);

	if (amdgpu_ras_intr_triggered())
		return ret;

	if (amdgpu_dpm_allow_xgmi_power_down(adev, true))
		dev_warn(adev->dev, "Failed to allow XGMI power down");

	if (amdgpu_dpm_set_df_cstate(adev, DF_CSTATE_ALLOW))
		dev_warn(adev->dev, "Failed to allow df cstate");

	return ret;
}

/* wrapper of psp_ras_trigger_error */
int amdgpu_ras_error_inject(struct amdgpu_device *adev,
		struct ras_inject_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ta_ras_trigger_error_input block_info = {
		.block_id =  amdgpu_ras_block_to_ta(info->head.block),
		.inject_error_type = amdgpu_ras_error_to_ta(info->head.type),
		.sub_block_index = info->head.sub_block_index,
		.address = info->address,
		.value = info->value,
	};
	int ret = 0;

	if (!obj)
		return -EINVAL;

	/* Calculate XGMI relative offset */
	if (adev->gmc.xgmi.num_physical_nodes > 1) {
		block_info.address =
			amdgpu_xgmi_get_relative_phy_addr(adev,
							  block_info.address);
	}

	switch (info->head.block) {
	case AMDGPU_RAS_BLOCK__GFX:
		if (adev->gfx.ras_funcs &&
		    adev->gfx.ras_funcs->ras_error_inject)
			ret = adev->gfx.ras_funcs->ras_error_inject(adev, info);
		else
			ret = -EINVAL;
		break;
	case AMDGPU_RAS_BLOCK__UMC:
	case AMDGPU_RAS_BLOCK__SDMA:
	case AMDGPU_RAS_BLOCK__MMHUB:
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		ret = psp_ras_trigger_error(&adev->psp, &block_info);
		break;
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
		ret = amdgpu_ras_error_inject_xgmi(adev, &block_info);
		break;
	default:
		dev_info(adev->dev, "%s error injection is not supported yet\n",
			 ras_block_str(info->head.block));
		ret = -EINVAL;
	}

	if (ret)
		dev_err(adev->dev, "ras inject %s failed %d\n",
			ras_block_str(info->head.block), ret);

	return ret;
}

/**
 * amdgpu_ras_query_error_count -- Get error counts of all IPs
 * adev: pointer to AMD GPU device
 * ce_count: pointer to an integer to be set to the count of correctible errors.
 * ue_count: pointer to an integer to be set to the count of uncorrectible
 * errors.
 *
 * If set, @ce_count or @ue_count, count and return the corresponding
 * error counts in those integer pointers. Return 0 if the device
 * supports RAS. Return -EOPNOTSUPP if the device doesn't support RAS.
 */
int amdgpu_ras_query_error_count(struct amdgpu_device *adev,
				 unsigned long *ce_count,
				 unsigned long *ue_count)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;
	unsigned long ce, ue;

	if (!adev->ras_enabled || !con)
		return -EOPNOTSUPP;

	/* Don't count since no reporting.
	 */
	if (!ce_count && !ue_count)
		return 0;

	ce = 0;
	ue = 0;
	list_for_each_entry(obj, &con->head, node) {
		struct ras_query_if info = {
			.head = obj->head,
		};
		int res;

		res = amdgpu_ras_query_error_status(adev, &info);
		if (res)
			return res;

		ce += info.ce_count;
		ue += info.ue_count;
	}

	if (ce_count)
		*ce_count = ce;

	if (ue_count)
		*ue_count = ue;

	return 0;
}
/* query/inject/cure end */


/* sysfs begin */

static int amdgpu_ras_badpages_read(struct amdgpu_device *adev,
		struct ras_badpage **bps, unsigned int *count);

static char *amdgpu_ras_badpage_flags_str(unsigned int flags)
{
	switch (flags) {
	case AMDGPU_RAS_RETIRE_PAGE_RESERVED:
		return "R";
	case AMDGPU_RAS_RETIRE_PAGE_PENDING:
		return "P";
	case AMDGPU_RAS_RETIRE_PAGE_FAULT:
	default:
		return "F";
	}
}

/**
 * DOC: AMDGPU RAS sysfs gpu_vram_bad_pages Interface
 *
 * It allows user to read the bad pages of vram on the gpu through
 * /sys/class/drm/card[0/1/2...]/device/ras/gpu_vram_bad_pages
 *
 * It outputs multiple lines, and each line stands for one gpu page.
 *
 * The format of one line is below,
 * gpu pfn : gpu page size : flags
 *
 * gpu pfn and gpu page size are printed in hex format.
 * flags can be one of below character,
 *
 * R: reserved, this gpu page is reserved and not able to use.
 *
 * P: pending for reserve, this gpu page is marked as bad, will be reserved
 * in next window of page_reserve.
 *
 * F: unable to reserve. this gpu page can't be reserved due to some reasons.
 *
 * Examples:
 *
 * .. code-block:: bash
 *
 *	0x00000001 : 0x00001000 : R
 *	0x00000002 : 0x00001000 : P
 *
 */

static ssize_t amdgpu_ras_sysfs_badpages_read(struct file *f,
		struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t ppos, size_t count)
{
	struct amdgpu_ras *con =
		container_of(attr, struct amdgpu_ras, badpages_attr);
	struct amdgpu_device *adev = con->adev;
	const unsigned int element_size =
		sizeof("0xabcdabcd : 0x12345678 : R\n") - 1;
	unsigned int start = div64_ul(ppos + element_size - 1, element_size);
	unsigned int end = div64_ul(ppos + count - 1, element_size);
	ssize_t s = 0;
	struct ras_badpage *bps = NULL;
	unsigned int bps_count = 0;

	memset(buf, 0, count);

	if (amdgpu_ras_badpages_read(adev, &bps, &bps_count))
		return 0;

	for (; start < end && start < bps_count; start++)
		s += scnprintf(&buf[s], element_size + 1,
				"0x%08x : 0x%08x : %1s\n",
				bps[start].bp,
				bps[start].size,
				amdgpu_ras_badpage_flags_str(bps[start].flags));

	kfree(bps);

	return s;
}

static ssize_t amdgpu_ras_sysfs_features_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct amdgpu_ras *con =
		container_of(attr, struct amdgpu_ras, features_attr);

	return scnprintf(buf, PAGE_SIZE, "feature mask: 0x%x\n", con->features);
}

static void amdgpu_ras_sysfs_remove_bad_page_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	sysfs_remove_file_from_group(&adev->dev->kobj,
				&con->badpages_attr.attr,
				RAS_FS_NAME);
}

static int amdgpu_ras_sysfs_remove_feature_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct attribute *attrs[] = {
		&con->features_attr.attr,
		NULL
	};
	struct attribute_group group = {
		.name = RAS_FS_NAME,
		.attrs = attrs,
	};

	sysfs_remove_group(&adev->dev->kobj, &group);

	return 0;
}

int amdgpu_ras_sysfs_create(struct amdgpu_device *adev,
		struct ras_fs_if *head)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &head->head);

	if (!obj || obj->attr_inuse)
		return -EINVAL;

	get_obj(obj);

	memcpy(obj->fs_data.sysfs_name,
			head->sysfs_name,
			sizeof(obj->fs_data.sysfs_name));

	obj->sysfs_attr = (struct device_attribute){
		.attr = {
			.name = obj->fs_data.sysfs_name,
			.mode = S_IRUGO,
		},
			.show = amdgpu_ras_sysfs_read,
	};
	sysfs_attr_init(&obj->sysfs_attr.attr);

	if (sysfs_add_file_to_group(&adev->dev->kobj,
				&obj->sysfs_attr.attr,
				RAS_FS_NAME)) {
		put_obj(obj);
		return -EINVAL;
	}

	obj->attr_inuse = 1;

	return 0;
}

int amdgpu_ras_sysfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, head);

	if (!obj || !obj->attr_inuse)
		return -EINVAL;

	sysfs_remove_file_from_group(&adev->dev->kobj,
				&obj->sysfs_attr.attr,
				RAS_FS_NAME);
	obj->attr_inuse = 0;
	put_obj(obj);

	return 0;
}

static int amdgpu_ras_sysfs_remove_all(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &con->head, node) {
		amdgpu_ras_sysfs_remove(adev, &obj->head);
	}

	if (amdgpu_bad_page_threshold != 0)
		amdgpu_ras_sysfs_remove_bad_page_node(adev);

	amdgpu_ras_sysfs_remove_feature_node(adev);

	return 0;
}
/* sysfs end */

/**
 * DOC: AMDGPU RAS Reboot Behavior for Unrecoverable Errors
 *
 * Normally when there is an uncorrectable error, the driver will reset
 * the GPU to recover.  However, in the event of an unrecoverable error,
 * the driver provides an interface to reboot the system automatically
 * in that event.
 *
 * The following file in debugfs provides that interface:
 * /sys/kernel/debug/dri/[0/1/2...]/ras/auto_reboot
 *
 * Usage:
 *
 * .. code-block:: bash
 *
 *	echo true > .../ras/auto_reboot
 *
 */
/* debugfs begin */
static struct dentry *amdgpu_ras_debugfs_create_ctrl_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct drm_minor  *minor = adev_to_drm(adev)->primary;
	struct dentry     *dir;

	dir = debugfs_create_dir(RAS_FS_NAME, minor->debugfs_root);
	debugfs_create_file("ras_ctrl", S_IWUGO | S_IRUGO, dir, adev,
			    &amdgpu_ras_debugfs_ctrl_ops);
	debugfs_create_file("ras_eeprom_reset", S_IWUGO | S_IRUGO, dir, adev,
			    &amdgpu_ras_debugfs_eeprom_ops);
	debugfs_create_u32("bad_page_cnt_threshold", 0444, dir,
			   &con->bad_page_cnt_threshold);
	debugfs_create_x32("ras_hw_enabled", 0444, dir, &adev->ras_hw_enabled);
	debugfs_create_x32("ras_enabled", 0444, dir, &adev->ras_enabled);

	/*
	 * After one uncorrectable error happens, usually GPU recovery will
	 * be scheduled. But due to the known problem in GPU recovery failing
	 * to bring GPU back, below interface provides one direct way to
	 * user to reboot system automatically in such case within
	 * ERREVENT_ATHUB_INTERRUPT generated. Normal GPU recovery routine
	 * will never be called.
	 */
	debugfs_create_bool("auto_reboot", S_IWUGO | S_IRUGO, dir, &con->reboot);

	/*
	 * User could set this not to clean up hardware's error count register
	 * of RAS IPs during ras recovery.
	 */
	debugfs_create_bool("disable_ras_err_cnt_harvest", 0644, dir,
			    &con->disable_ras_err_cnt_harvest);
	return dir;
}

static void amdgpu_ras_debugfs_create(struct amdgpu_device *adev,
				      struct ras_fs_if *head,
				      struct dentry *dir)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &head->head);

	if (!obj || !dir)
		return;

	get_obj(obj);

	memcpy(obj->fs_data.debugfs_name,
			head->debugfs_name,
			sizeof(obj->fs_data.debugfs_name));

	debugfs_create_file(obj->fs_data.debugfs_name, S_IWUGO | S_IRUGO, dir,
			    obj, &amdgpu_ras_debugfs_ops);
}

void amdgpu_ras_debugfs_create_all(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct dentry *dir;
	struct ras_manager *obj;
	struct ras_fs_if fs_info;

	/*
	 * it won't be called in resume path, no need to check
	 * suspend and gpu reset status
	 */
	if (!IS_ENABLED(CONFIG_DEBUG_FS) || !con)
		return;

	dir = amdgpu_ras_debugfs_create_ctrl_node(adev);

	list_for_each_entry(obj, &con->head, node) {
		if (amdgpu_ras_is_supported(adev, obj->head.block) &&
			(obj->attr_inuse == 1)) {
			sprintf(fs_info.debugfs_name, "%s_err_inject",
					ras_block_str(obj->head.block));
			fs_info.head = obj->head;
			amdgpu_ras_debugfs_create(adev, &fs_info, dir);
		}
	}
}

/* debugfs end */

/* ras fs */
static BIN_ATTR(gpu_vram_bad_pages, S_IRUGO,
		amdgpu_ras_sysfs_badpages_read, NULL, 0);
static DEVICE_ATTR(features, S_IRUGO,
		amdgpu_ras_sysfs_features_read, NULL);
static int amdgpu_ras_fs_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct attribute_group group = {
		.name = RAS_FS_NAME,
	};
	struct attribute *attrs[] = {
		&con->features_attr.attr,
		NULL
	};
	struct bin_attribute *bin_attrs[] = {
		NULL,
		NULL,
	};
	int r;

	/* add features entry */
	con->features_attr = dev_attr_features;
	group.attrs = attrs;
	sysfs_attr_init(attrs[0]);

	if (amdgpu_bad_page_threshold != 0) {
		/* add bad_page_features entry */
		bin_attr_gpu_vram_bad_pages.private = NULL;
		con->badpages_attr = bin_attr_gpu_vram_bad_pages;
		bin_attrs[0] = &con->badpages_attr;
		group.bin_attrs = bin_attrs;
		sysfs_bin_attr_init(bin_attrs[0]);
	}

	r = sysfs_create_group(&adev->dev->kobj, &group);
	if (r)
		dev_err(adev->dev, "Failed to create RAS sysfs group!");

	return 0;
}

static int amdgpu_ras_fs_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *con_obj, *ip_obj, *tmp;

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		list_for_each_entry_safe(con_obj, tmp, &con->head, node) {
			ip_obj = amdgpu_ras_find_obj(adev, &con_obj->head);
			if (ip_obj)
				put_obj(ip_obj);
		}
	}

	amdgpu_ras_sysfs_remove_all(adev);
	return 0;
}
/* ras fs end */

/* ih begin */
static void amdgpu_ras_interrupt_handler(struct ras_manager *obj)
{
	struct ras_ih_data *data = &obj->ih_data;
	struct amdgpu_iv_entry entry;
	int ret;
	struct ras_err_data err_data = {0, 0, 0, NULL};

	while (data->rptr != data->wptr) {
		rmb();
		memcpy(&entry, &data->ring[data->rptr],
				data->element_size);

		wmb();
		data->rptr = (data->aligned_element_size +
				data->rptr) % data->ring_size;

		/* Let IP handle its data, maybe we need get the output
		 * from the callback to udpate the error type/count, etc
		 */
		if (data->cb) {
			ret = data->cb(obj->adev, &err_data, &entry);
			/* ue will trigger an interrupt, and in that case
			 * we need do a reset to recovery the whole system.
			 * But leave IP do that recovery, here we just dispatch
			 * the error.
			 */
			if (ret == AMDGPU_RAS_SUCCESS) {
				/* these counts could be left as 0 if
				 * some blocks do not count error number
				 */
				obj->err_data.ue_count += err_data.ue_count;
				obj->err_data.ce_count += err_data.ce_count;
			}
		}
	}
}

static void amdgpu_ras_interrupt_process_handler(struct work_struct *work)
{
	struct ras_ih_data *data =
		container_of(work, struct ras_ih_data, ih_work);
	struct ras_manager *obj =
		container_of(data, struct ras_manager, ih_data);

	amdgpu_ras_interrupt_handler(obj);
}

int amdgpu_ras_interrupt_dispatch(struct amdgpu_device *adev,
		struct ras_dispatch_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ras_ih_data *data = &obj->ih_data;

	if (!obj)
		return -EINVAL;

	if (data->inuse == 0)
		return 0;

	/* Might be overflow... */
	memcpy(&data->ring[data->wptr], info->entry,
			data->element_size);

	wmb();
	data->wptr = (data->aligned_element_size +
			data->wptr) % data->ring_size;

	schedule_work(&data->ih_work);

	return 0;
}

int amdgpu_ras_interrupt_remove_handler(struct amdgpu_device *adev,
		struct ras_ih_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ras_ih_data *data;

	if (!obj)
		return -EINVAL;

	data = &obj->ih_data;
	if (data->inuse == 0)
		return 0;

	cancel_work_sync(&data->ih_work);

	kfree(data->ring);
	memset(data, 0, sizeof(*data));
	put_obj(obj);

	return 0;
}

int amdgpu_ras_interrupt_add_handler(struct amdgpu_device *adev,
		struct ras_ih_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ras_ih_data *data;

	if (!obj) {
		/* in case we registe the IH before enable ras feature */
		obj = amdgpu_ras_create_obj(adev, &info->head);
		if (!obj)
			return -EINVAL;
	} else
		get_obj(obj);

	data = &obj->ih_data;
	/* add the callback.etc */
	*data = (struct ras_ih_data) {
		.inuse = 0,
		.cb = info->cb,
		.element_size = sizeof(struct amdgpu_iv_entry),
		.rptr = 0,
		.wptr = 0,
	};

	INIT_WORK(&data->ih_work, amdgpu_ras_interrupt_process_handler);

	data->aligned_element_size = ALIGN(data->element_size, 8);
	/* the ring can store 64 iv entries. */
	data->ring_size = 64 * data->aligned_element_size;
	data->ring = kmalloc(data->ring_size, GFP_KERNEL);
	if (!data->ring) {
		put_obj(obj);
		return -ENOMEM;
	}

	/* IH is ready */
	data->inuse = 1;

	return 0;
}

static int amdgpu_ras_interrupt_remove_all(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &con->head, node) {
		struct ras_ih_if info = {
			.head = obj->head,
		};
		amdgpu_ras_interrupt_remove_handler(adev, &info);
	}

	return 0;
}
/* ih end */

/* traversal all IPs except NBIO to query error counter */
static void amdgpu_ras_log_on_err_counter(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;

	if (!adev->ras_enabled || !con)
		return;

	list_for_each_entry(obj, &con->head, node) {
		struct ras_query_if info = {
			.head = obj->head,
		};

		/*
		 * PCIE_BIF IP has one different isr by ras controller
		 * interrupt, the specific ras counter query will be
		 * done in that isr. So skip such block from common
		 * sync flood interrupt isr calling.
		 */
		if (info.head.block == AMDGPU_RAS_BLOCK__PCIE_BIF)
			continue;

		amdgpu_ras_query_error_status(adev, &info);
	}
}

/* Parse RdRspStatus and WrRspStatus */
static void amdgpu_ras_error_status_query(struct amdgpu_device *adev,
					  struct ras_query_if *info)
{
	/*
	 * Only two block need to query read/write
	 * RspStatus at current state
	 */
	switch (info->head.block) {
	case AMDGPU_RAS_BLOCK__GFX:
		if (adev->gfx.ras_funcs &&
		    adev->gfx.ras_funcs->query_ras_error_status)
			adev->gfx.ras_funcs->query_ras_error_status(adev);
		break;
	case AMDGPU_RAS_BLOCK__MMHUB:
		if (adev->mmhub.ras_funcs &&
		    adev->mmhub.ras_funcs->query_ras_error_status)
			adev->mmhub.ras_funcs->query_ras_error_status(adev);
		break;
	default:
		break;
	}
}

static void amdgpu_ras_query_err_status(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;

	if (!adev->ras_enabled || !con)
		return;

	list_for_each_entry(obj, &con->head, node) {
		struct ras_query_if info = {
			.head = obj->head,
		};

		amdgpu_ras_error_status_query(adev, &info);
	}
}

/* recovery begin */

/* return 0 on success.
 * caller need free bps.
 */
static int amdgpu_ras_badpages_read(struct amdgpu_device *adev,
		struct ras_badpage **bps, unsigned int *count)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data;
	int i = 0;
	int ret = 0, status;

	if (!con || !con->eh_data || !bps || !count)
		return -EINVAL;

	mutex_lock(&con->recovery_lock);
	data = con->eh_data;
	if (!data || data->count == 0) {
		*bps = NULL;
		ret = -EINVAL;
		goto out;
	}

	*bps = kmalloc(sizeof(struct ras_badpage) * data->count, GFP_KERNEL);
	if (!*bps) {
		ret = -ENOMEM;
		goto out;
	}

	for (; i < data->count; i++) {
		(*bps)[i] = (struct ras_badpage){
			.bp = data->bps[i].retired_page,
			.size = AMDGPU_GPU_PAGE_SIZE,
			.flags = AMDGPU_RAS_RETIRE_PAGE_RESERVED,
		};
		status = amdgpu_vram_mgr_query_page_status(
				ttm_manager_type(&adev->mman.bdev, TTM_PL_VRAM),
				data->bps[i].retired_page);
		if (status == -EBUSY)
			(*bps)[i].flags = AMDGPU_RAS_RETIRE_PAGE_PENDING;
		else if (status == -ENOENT)
			(*bps)[i].flags = AMDGPU_RAS_RETIRE_PAGE_FAULT;
	}

	*count = data->count;
out:
	mutex_unlock(&con->recovery_lock);
	return ret;
}

static void amdgpu_ras_do_recovery(struct work_struct *work)
{
	struct amdgpu_ras *ras =
		container_of(work, struct amdgpu_ras, recovery_work);
	struct amdgpu_device *remote_adev = NULL;
	struct amdgpu_device *adev = ras->adev;
	struct list_head device_list, *device_list_handle =  NULL;

	if (!ras->disable_ras_err_cnt_harvest) {
		struct amdgpu_hive_info *hive = amdgpu_get_xgmi_hive(adev);

		/* Build list of devices to query RAS related errors */
		if  (hive && adev->gmc.xgmi.num_physical_nodes > 1) {
			device_list_handle = &hive->device_list;
		} else {
			INIT_LIST_HEAD(&device_list);
			list_add_tail(&adev->gmc.xgmi.head, &device_list);
			device_list_handle = &device_list;
		}

		list_for_each_entry(remote_adev,
				device_list_handle, gmc.xgmi.head) {
			amdgpu_ras_query_err_status(remote_adev);
			amdgpu_ras_log_on_err_counter(remote_adev);
		}

		amdgpu_put_xgmi_hive(hive);
	}

	if (amdgpu_device_should_recover_gpu(ras->adev))
		amdgpu_device_gpu_recover(ras->adev, NULL);
	atomic_set(&ras->in_recovery, 0);
}

/* alloc/realloc bps array */
static int amdgpu_ras_realloc_eh_data_space(struct amdgpu_device *adev,
		struct ras_err_handler_data *data, int pages)
{
	unsigned int old_space = data->count + data->space_left;
	unsigned int new_space = old_space + pages;
	unsigned int align_space = ALIGN(new_space, 512);
	void *bps = kmalloc(align_space * sizeof(*data->bps), GFP_KERNEL);

	if (!bps) {
		kfree(bps);
		return -ENOMEM;
	}

	if (data->bps) {
		memcpy(bps, data->bps,
				data->count * sizeof(*data->bps));
		kfree(data->bps);
	}

	data->bps = bps;
	data->space_left += align_space - old_space;
	return 0;
}

/* it deal with vram only. */
int amdgpu_ras_add_bad_pages(struct amdgpu_device *adev,
		struct eeprom_table_record *bps, int pages)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data;
	int ret = 0;
	uint32_t i;

	if (!con || !con->eh_data || !bps || pages <= 0)
		return 0;

	mutex_lock(&con->recovery_lock);
	data = con->eh_data;
	if (!data)
		goto out;

	for (i = 0; i < pages; i++) {
		if (amdgpu_ras_check_bad_page_unlock(con,
			bps[i].retired_page << AMDGPU_GPU_PAGE_SHIFT))
			continue;

		if (!data->space_left &&
			amdgpu_ras_realloc_eh_data_space(adev, data, 256)) {
			ret = -ENOMEM;
			goto out;
		}

		amdgpu_vram_mgr_reserve_range(
			ttm_manager_type(&adev->mman.bdev, TTM_PL_VRAM),
			bps[i].retired_page << AMDGPU_GPU_PAGE_SHIFT,
			AMDGPU_GPU_PAGE_SIZE);

		memcpy(&data->bps[data->count], &bps[i], sizeof(*data->bps));
		data->count++;
		data->space_left--;
	}
out:
	mutex_unlock(&con->recovery_lock);

	return ret;
}

/*
 * write error record array to eeprom, the function should be
 * protected by recovery_lock
 */
int amdgpu_ras_save_bad_pages(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data;
	struct amdgpu_ras_eeprom_control *control;
	int save_count;

	if (!con || !con->eh_data)
		return 0;

	control = &con->eeprom_control;
	data = con->eh_data;
	save_count = data->count - control->num_recs;
	/* only new entries are saved */
	if (save_count > 0) {
		if (amdgpu_ras_eeprom_process_recods(control,
							&data->bps[control->num_recs],
							true,
							save_count)) {
			dev_err(adev->dev, "Failed to save EEPROM table data!");
			return -EIO;
		}

		dev_info(adev->dev, "Saved %d pages to EEPROM table.\n", save_count);
	}

	return 0;
}

/*
 * read error record array in eeprom and reserve enough space for
 * storing new bad pages
 */
static int amdgpu_ras_load_bad_pages(struct amdgpu_device *adev)
{
	struct amdgpu_ras_eeprom_control *control =
					&adev->psp.ras.ras->eeprom_control;
	struct eeprom_table_record *bps = NULL;
	int ret = 0;

	/* no bad page record, skip eeprom access */
	if (!control->num_recs || (amdgpu_bad_page_threshold == 0))
		return ret;

	bps = kcalloc(control->num_recs, sizeof(*bps), GFP_KERNEL);
	if (!bps)
		return -ENOMEM;

	if (amdgpu_ras_eeprom_process_recods(control, bps, false,
		control->num_recs)) {
		dev_err(adev->dev, "Failed to load EEPROM table records!");
		ret = -EIO;
		goto out;
	}

	ret = amdgpu_ras_add_bad_pages(adev, bps, control->num_recs);

out:
	kfree(bps);
	return ret;
}

static bool amdgpu_ras_check_bad_page_unlock(struct amdgpu_ras *con,
				uint64_t addr)
{
	struct ras_err_handler_data *data = con->eh_data;
	int i;

	addr >>= AMDGPU_GPU_PAGE_SHIFT;
	for (i = 0; i < data->count; i++)
		if (addr == data->bps[i].retired_page)
			return true;

	return false;
}

/*
 * check if an address belongs to bad page
 *
 * Note: this check is only for umc block
 */
static bool amdgpu_ras_check_bad_page(struct amdgpu_device *adev,
				uint64_t addr)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	bool ret = false;

	if (!con || !con->eh_data)
		return ret;

	mutex_lock(&con->recovery_lock);
	ret = amdgpu_ras_check_bad_page_unlock(con, addr);
	mutex_unlock(&con->recovery_lock);
	return ret;
}

static void amdgpu_ras_validate_threshold(struct amdgpu_device *adev,
					uint32_t max_length)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int tmp_threshold = amdgpu_bad_page_threshold;
	u64 val;

	/*
	 * Justification of value bad_page_cnt_threshold in ras structure
	 *
	 * Generally, -1 <= amdgpu_bad_page_threshold <= max record length
	 * in eeprom, and introduce two scenarios accordingly.
	 *
	 * Bad page retirement enablement:
	 *    - If amdgpu_bad_page_threshold = -1,
	 *      bad_page_cnt_threshold = typical value by formula.
	 *
	 *    - When the value from user is 0 < amdgpu_bad_page_threshold <
	 *      max record length in eeprom, use it directly.
	 *
	 * Bad page retirement disablement:
	 *    - If amdgpu_bad_page_threshold = 0, bad page retirement
	 *      functionality is disabled, and bad_page_cnt_threshold will
	 *      take no effect.
	 */

	if (tmp_threshold < -1)
		tmp_threshold = -1;
	else if (tmp_threshold > max_length)
		tmp_threshold = max_length;

	if (tmp_threshold == -1) {
		val = adev->gmc.mc_vram_size;
		do_div(val, RAS_BAD_PAGE_RATE);
		con->bad_page_cnt_threshold = min(lower_32_bits(val),
						max_length);
	} else {
		con->bad_page_cnt_threshold = tmp_threshold;
	}
}

int amdgpu_ras_recovery_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data **data;
	uint32_t max_eeprom_records_len = 0;
	bool exc_err_limit = false;
	int ret;

	if (adev->ras_enabled && con)
		data = &con->eh_data;
	else
		return 0;

	*data = kmalloc(sizeof(**data), GFP_KERNEL | __GFP_ZERO);
	if (!*data) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_init(&con->recovery_lock);
	INIT_WORK(&con->recovery_work, amdgpu_ras_do_recovery);
	atomic_set(&con->in_recovery, 0);
	con->adev = adev;

	max_eeprom_records_len = amdgpu_ras_eeprom_get_record_max_length();
	amdgpu_ras_validate_threshold(adev, max_eeprom_records_len);

	/* Todo: During test the SMU might fail to read the eeprom through I2C
	 * when the GPU is pending on XGMI reset during probe time
	 * (Mostly after second bus reset), skip it now
	 */
	if (adev->gmc.xgmi.pending_reset)
		return 0;
	ret = amdgpu_ras_eeprom_init(&con->eeprom_control, &exc_err_limit);
	/*
	 * This calling fails when exc_err_limit is true or
	 * ret != 0.
	 */
	if (exc_err_limit || ret)
		goto free;

	if (con->eeprom_control.num_recs) {
		ret = amdgpu_ras_load_bad_pages(adev);
		if (ret)
			goto free;

		if (adev->smu.ppt_funcs && adev->smu.ppt_funcs->send_hbm_bad_pages_num)
			adev->smu.ppt_funcs->send_hbm_bad_pages_num(&adev->smu, con->eeprom_control.num_recs);
	}

	return 0;

free:
	kfree((*data)->bps);
	kfree(*data);
	con->eh_data = NULL;
out:
	dev_warn(adev->dev, "Failed to initialize ras recovery!\n");

	/*
	 * Except error threshold exceeding case, other failure cases in this
	 * function would not fail amdgpu driver init.
	 */
	if (!exc_err_limit)
		ret = 0;
	else
		ret = -EINVAL;

	return ret;
}

static int amdgpu_ras_recovery_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data = con->eh_data;

	/* recovery_init failed to init it, fini is useless */
	if (!data)
		return 0;

	cancel_work_sync(&con->recovery_work);

	mutex_lock(&con->recovery_lock);
	con->eh_data = NULL;
	kfree(data->bps);
	kfree(data);
	mutex_unlock(&con->recovery_lock);

	return 0;
}
/* recovery end */

/* return 0 if ras will reset gpu and repost.*/
int amdgpu_ras_request_reset_on_boot(struct amdgpu_device *adev,
		unsigned int block)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (!ras)
		return -EINVAL;

	ras->flags |= AMDGPU_RAS_FLAG_INIT_NEED_RESET;
	return 0;
}

static bool amdgpu_ras_asic_supported(struct amdgpu_device *adev)
{
	return adev->asic_type == CHIP_VEGA10 ||
		adev->asic_type == CHIP_VEGA20 ||
		adev->asic_type == CHIP_ARCTURUS ||
		adev->asic_type == CHIP_ALDEBARAN ||
		adev->asic_type == CHIP_SIENNA_CICHLID;
}

/*
 * this is workaround for vega20 workstation sku,
 * force enable gfx ras, ignore vbios gfx ras flag
 * due to GC EDC can not write
 */
static void amdgpu_ras_get_quirks(struct amdgpu_device *adev)
{
	struct atom_context *ctx = adev->mode_info.atom_context;

	if (!ctx)
		return;

	if (strnstr(ctx->vbios_version, "D16406",
		    sizeof(ctx->vbios_version)) ||
		strnstr(ctx->vbios_version, "D36002",
			sizeof(ctx->vbios_version)))
		adev->ras_hw_enabled |= (1 << AMDGPU_RAS_BLOCK__GFX);
}

/*
 * check hardware's ras ability which will be saved in hw_supported.
 * if hardware does not support ras, we can skip some ras initializtion and
 * forbid some ras operations from IP.
 * if software itself, say boot parameter, limit the ras ability. We still
 * need allow IP do some limited operations, like disable. In such case,
 * we have to initialize ras as normal. but need check if operation is
 * allowed or not in each function.
 */
static void amdgpu_ras_check_supported(struct amdgpu_device *adev)
{
	adev->ras_hw_enabled = adev->ras_enabled = 0;

	if (amdgpu_sriov_vf(adev) || !adev->is_atom_fw ||
	    !amdgpu_ras_asic_supported(adev))
		return;

	if (!adev->gmc.xgmi.connected_to_cpu) {
		if (amdgpu_atomfirmware_mem_ecc_supported(adev)) {
			dev_info(adev->dev, "MEM ECC is active.\n");
			adev->ras_hw_enabled |= (1 << AMDGPU_RAS_BLOCK__UMC |
						   1 << AMDGPU_RAS_BLOCK__DF);
		} else {
			dev_info(adev->dev, "MEM ECC is not presented.\n");
		}

		if (amdgpu_atomfirmware_sram_ecc_supported(adev)) {
			dev_info(adev->dev, "SRAM ECC is active.\n");
			adev->ras_hw_enabled |= ~(1 << AMDGPU_RAS_BLOCK__UMC |
						    1 << AMDGPU_RAS_BLOCK__DF);
		} else {
			dev_info(adev->dev, "SRAM ECC is not presented.\n");
		}
	} else {
		/* driver only manages a few IP blocks RAS feature
		 * when GPU is connected cpu through XGMI */
		adev->ras_hw_enabled |= (1 << AMDGPU_RAS_BLOCK__GFX |
					   1 << AMDGPU_RAS_BLOCK__SDMA |
					   1 << AMDGPU_RAS_BLOCK__MMHUB);
	}

	amdgpu_ras_get_quirks(adev);

	/* hw_supported needs to be aligned with RAS block mask. */
	adev->ras_hw_enabled &= AMDGPU_RAS_BLOCK_MASK;

	adev->ras_enabled = amdgpu_ras_enable == 0 ? 0 :
		adev->ras_hw_enabled & amdgpu_ras_mask;
}

static void amdgpu_ras_counte_dw(struct work_struct *work)
{
	struct amdgpu_ras *con = container_of(work, struct amdgpu_ras,
					      ras_counte_delay_work.work);
	struct amdgpu_device *adev = con->adev;
	struct drm_device *dev = adev_to_drm(adev);
	unsigned long ce_count, ue_count;
	int res;

	res = pm_runtime_get_sync(dev->dev);
	if (res < 0)
		goto Out;

	/* Cache new values.
	 */
	if (amdgpu_ras_query_error_count(adev, &ce_count, &ue_count) == 0) {
		atomic_set(&con->ras_ce_count, ce_count);
		atomic_set(&con->ras_ue_count, ue_count);
	}

	pm_runtime_mark_last_busy(dev->dev);
Out:
	pm_runtime_put_autosuspend(dev->dev);
}

int amdgpu_ras_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int r;

	if (con)
		return 0;

	con = kmalloc(sizeof(struct amdgpu_ras) +
			sizeof(struct ras_manager) * AMDGPU_RAS_BLOCK_COUNT,
			GFP_KERNEL|__GFP_ZERO);
	if (!con)
		return -ENOMEM;

	con->adev = adev;
	INIT_DELAYED_WORK(&con->ras_counte_delay_work, amdgpu_ras_counte_dw);
	atomic_set(&con->ras_ce_count, 0);
	atomic_set(&con->ras_ue_count, 0);

	con->objs = (struct ras_manager *)(con + 1);

	amdgpu_ras_set_context(adev, con);

	amdgpu_ras_check_supported(adev);

	if (!adev->ras_enabled || adev->asic_type == CHIP_VEGA10) {
		/* set gfx block ras context feature for VEGA20 Gaming
		 * send ras disable cmd to ras ta during ras late init.
		 */
		if (!adev->ras_enabled && adev->asic_type == CHIP_VEGA20) {
			con->features |= BIT(AMDGPU_RAS_BLOCK__GFX);

			return 0;
		}

		r = 0;
		goto release_con;
	}

	con->features = 0;
	INIT_LIST_HEAD(&con->head);
	/* Might need get this flag from vbios. */
	con->flags = RAS_DEFAULT_FLAGS;

	/* initialize nbio ras function ahead of any other
	 * ras functions so hardware fatal error interrupt
	 * can be enabled as early as possible */
	switch (adev->asic_type) {
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
	case CHIP_ALDEBARAN:
		if (!adev->gmc.xgmi.connected_to_cpu)
			adev->nbio.ras_funcs = &nbio_v7_4_ras_funcs;
		break;
	default:
		/* nbio ras is not available */
		break;
	}

	if (adev->nbio.ras_funcs &&
	    adev->nbio.ras_funcs->init_ras_controller_interrupt) {
		r = adev->nbio.ras_funcs->init_ras_controller_interrupt(adev);
		if (r)
			goto release_con;
	}

	if (adev->nbio.ras_funcs &&
	    adev->nbio.ras_funcs->init_ras_err_event_athub_interrupt) {
		r = adev->nbio.ras_funcs->init_ras_err_event_athub_interrupt(adev);
		if (r)
			goto release_con;
	}

	if (amdgpu_ras_fs_init(adev)) {
		r = -EINVAL;
		goto release_con;
	}

	dev_info(adev->dev, "RAS INFO: ras initialized successfully, "
		 "hardware ability[%x] ras_mask[%x]\n",
		 adev->ras_hw_enabled, adev->ras_enabled);

	return 0;
release_con:
	amdgpu_ras_set_context(adev, NULL);
	kfree(con);

	return r;
}

int amdgpu_persistent_edc_harvesting_supported(struct amdgpu_device *adev)
{
	if (adev->gmc.xgmi.connected_to_cpu)
		return 1;
	return 0;
}

static int amdgpu_persistent_edc_harvesting(struct amdgpu_device *adev,
					struct ras_common_if *ras_block)
{
	struct ras_query_if info = {
		.head = *ras_block,
	};

	if (!amdgpu_persistent_edc_harvesting_supported(adev))
		return 0;

	if (amdgpu_ras_query_error_status(adev, &info) != 0)
		DRM_WARN("RAS init harvest failure");

	if (amdgpu_ras_reset_error_status(adev, ras_block->block) != 0)
		DRM_WARN("RAS init harvest reset failure");

	return 0;
}

/* helper function to handle common stuff in ip late init phase */
int amdgpu_ras_late_init(struct amdgpu_device *adev,
			 struct ras_common_if *ras_block,
			 struct ras_fs_if *fs_info,
			 struct ras_ih_if *ih_info)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	unsigned long ue_count, ce_count;
	int r;

	/* disable RAS feature per IP block if it is not supported */
	if (!amdgpu_ras_is_supported(adev, ras_block->block)) {
		amdgpu_ras_feature_enable_on_boot(adev, ras_block, 0);
		return 0;
	}

	r = amdgpu_ras_feature_enable_on_boot(adev, ras_block, 1);
	if (r) {
		if (r == -EAGAIN) {
			/* request gpu reset. will run again */
			amdgpu_ras_request_reset_on_boot(adev,
					ras_block->block);
			return 0;
		} else if (adev->in_suspend || amdgpu_in_reset(adev)) {
			/* in resume phase, if fail to enable ras,
			 * clean up all ras fs nodes, and disable ras */
			goto cleanup;
		} else
			return r;
	}

	/* check for errors on warm reset edc persisant supported ASIC */
	amdgpu_persistent_edc_harvesting(adev, ras_block);

	/* in resume phase, no need to create ras fs node */
	if (adev->in_suspend || amdgpu_in_reset(adev))
		return 0;

	if (ih_info->cb) {
		r = amdgpu_ras_interrupt_add_handler(adev, ih_info);
		if (r)
			goto interrupt;
	}

	r = amdgpu_ras_sysfs_create(adev, fs_info);
	if (r)
		goto sysfs;

	/* Those are the cached values at init.
	 */
	if (amdgpu_ras_query_error_count(adev, &ce_count, &ue_count) == 0) {
		atomic_set(&con->ras_ce_count, ce_count);
		atomic_set(&con->ras_ue_count, ue_count);
	}

	return 0;
cleanup:
	amdgpu_ras_sysfs_remove(adev, ras_block);
sysfs:
	if (ih_info->cb)
		amdgpu_ras_interrupt_remove_handler(adev, ih_info);
interrupt:
	amdgpu_ras_feature_enable(adev, ras_block, 0);
	return r;
}

/* helper function to remove ras fs node and interrupt handler */
void amdgpu_ras_late_fini(struct amdgpu_device *adev,
			  struct ras_common_if *ras_block,
			  struct ras_ih_if *ih_info)
{
	if (!ras_block || !ih_info)
		return;

	amdgpu_ras_sysfs_remove(adev, ras_block);
	if (ih_info->cb)
		amdgpu_ras_interrupt_remove_handler(adev, ih_info);
	amdgpu_ras_feature_enable(adev, ras_block, 0);
}

/* do some init work after IP late init as dependence.
 * and it runs in resume/gpu reset/booting up cases.
 */
void amdgpu_ras_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj, *tmp;

	if (!adev->ras_enabled || !con) {
		/* clean ras context for VEGA20 Gaming after send ras disable cmd */
		amdgpu_release_ras_context(adev);

		return;
	}

	if (con->flags & AMDGPU_RAS_FLAG_INIT_BY_VBIOS) {
		/* Set up all other IPs which are not implemented. There is a
		 * tricky thing that IP's actual ras error type should be
		 * MULTI_UNCORRECTABLE, but as driver does not handle it, so
		 * ERROR_NONE make sense anyway.
		 */
		amdgpu_ras_enable_all_features(adev, 1);

		/* We enable ras on all hw_supported block, but as boot
		 * parameter might disable some of them and one or more IP has
		 * not implemented yet. So we disable them on behalf.
		 */
		list_for_each_entry_safe(obj, tmp, &con->head, node) {
			if (!amdgpu_ras_is_supported(adev, obj->head.block)) {
				amdgpu_ras_feature_enable(adev, &obj->head, 0);
				/* there should be no any reference. */
				WARN_ON(alive_obj(obj));
			}
		}
	}

	if (con->flags & AMDGPU_RAS_FLAG_INIT_NEED_RESET) {
		con->flags &= ~AMDGPU_RAS_FLAG_INIT_NEED_RESET;
		/* setup ras obj state as disabled.
		 * for init_by_vbios case.
		 * if we want to enable ras, just enable it in a normal way.
		 * If we want do disable it, need setup ras obj as enabled,
		 * then issue another TA disable cmd.
		 * See feature_enable_on_boot
		 */
		amdgpu_ras_disable_all_features(adev, 1);
		amdgpu_ras_reset_gpu(adev);
	}
}

void amdgpu_ras_suspend(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!adev->ras_enabled || !con)
		return;

	amdgpu_ras_disable_all_features(adev, 0);
	/* Make sure all ras objects are disabled. */
	if (con->features)
		amdgpu_ras_disable_all_features(adev, 1);
}

/* do some fini work before IP fini as dependence */
int amdgpu_ras_pre_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!adev->ras_enabled || !con)
		return 0;


	/* Need disable ras on all IPs here before ip [hw/sw]fini */
	amdgpu_ras_disable_all_features(adev, 0);
	amdgpu_ras_recovery_fini(adev);
	return 0;
}

int amdgpu_ras_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!adev->ras_enabled || !con)
		return 0;

	amdgpu_ras_fs_fini(adev);
	amdgpu_ras_interrupt_remove_all(adev);

	WARN(con->features, "Feature mask is not cleared");

	if (con->features)
		amdgpu_ras_disable_all_features(adev, 1);

	cancel_delayed_work_sync(&con->ras_counte_delay_work);

	amdgpu_ras_set_context(adev, NULL);
	kfree(con);

	return 0;
}

void amdgpu_ras_global_ras_isr(struct amdgpu_device *adev)
{
	amdgpu_ras_check_supported(adev);
	if (!adev->ras_hw_enabled)
		return;

	if (atomic_cmpxchg(&amdgpu_ras_in_intr, 0, 1) == 0) {
		dev_info(adev->dev, "uncorrectable hardware error"
			"(ERREVENT_ATHUB_INTERRUPT) detected!\n");

		amdgpu_ras_reset_gpu(adev);
	}
}

bool amdgpu_ras_need_emergency_restart(struct amdgpu_device *adev)
{
	if (adev->asic_type == CHIP_VEGA20 &&
	    adev->pm.fw_version <= 0x283400) {
		return !(amdgpu_asic_reset_method(adev) == AMD_RESET_METHOD_BACO) &&
				amdgpu_ras_intr_triggered();
	}

	return false;
}

void amdgpu_release_ras_context(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!con)
		return;

	if (!adev->ras_enabled && con->features & BIT(AMDGPU_RAS_BLOCK__GFX)) {
		con->features &= ~BIT(AMDGPU_RAS_BLOCK__GFX);
		amdgpu_ras_set_context(adev, NULL);
		kfree(con);
	}
}
