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
#include <linux/list_sort.h>

#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_xgmi.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"
#include "nbio_v4_3.h"
#include "nbio_v7_9.h"
#include "atom.h"
#include "amdgpu_reset.h"

#ifdef CONFIG_X86_MCE_AMD
#include <asm/mce.h>

static bool notifier_registered;
#endif
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
	"mca",
	"vcn",
	"jpeg",
};

const char *ras_mca_block_string[] = {
	"mca_mp0",
	"mca_mp1",
	"mca_mpio",
	"mca_iohc",
};

struct amdgpu_ras_block_list {
	/* ras block link */
	struct list_head node;

	struct amdgpu_ras_block_object *ras_obj;
};

const char *get_ras_block_str(struct ras_common_if *ras_block)
{
	if (!ras_block)
		return "NULL";

	if (ras_block->block >= AMDGPU_RAS_BLOCK_COUNT)
		return "OUT OF RANGE";

	if (ras_block->block == AMDGPU_RAS_BLOCK__MCA)
		return ras_mca_block_string[ras_block->sub_block_index];

	return ras_block_string[ras_block->block];
}

#define ras_block_str(_BLOCK_) \
	(((_BLOCK_) < ARRAY_SIZE(ras_block_string)) ? ras_block_string[_BLOCK_] : "Out Of Range")

#define ras_err_str(i) (ras_error_string[ffs(i)])

#define RAS_DEFAULT_FLAGS (AMDGPU_RAS_FLAG_INIT_BY_VBIOS)

/* inject address is 52 bits */
#define	RAS_UMC_INJECT_ADDR_LIMIT	(0x1ULL << 52)

/* typical ECC bad page rate is 1 bad page per 100MB VRAM */
#define RAS_BAD_PAGE_COVER              (100 * 1024 * 1024ULL)

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
#ifdef CONFIG_X86_MCE_AMD
static void amdgpu_register_bad_pages_mca_notifier(struct amdgpu_device *adev);
struct mce_notifier_adev_list {
	struct amdgpu_device *devs[MAX_GPU_INSTANCE];
	int num_gpu;
};
static struct mce_notifier_adev_list mce_adev_list;
#endif

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
	struct ras_err_data err_data;
	struct eeprom_table_record err_rec;
	int ret;

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

	ret = amdgpu_ras_error_data_init(&err_data);
	if (ret)
		return ret;

	memset(&err_rec, 0x0, sizeof(struct eeprom_table_record));
	err_data.err_addr = &err_rec;
	amdgpu_umc_fill_error_record(&err_data, address, address, 0, 0);

	if (amdgpu_bad_page_threshold != 0) {
		amdgpu_ras_add_bad_pages(adev, err_data.err_addr,
					 err_data.err_addr_cnt);
		amdgpu_ras_save_bad_pages(adev, NULL);
	}

	amdgpu_ras_error_data_fini(&err_data);

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

	/* Hardware counter will be reset automatically after the query on Vega20 and Arcturus */
	if (amdgpu_ip_version(obj->adev, MP0_HWIP, 0) != IP_VERSION(11, 0, 2) &&
	    amdgpu_ip_version(obj->adev, MP0_HWIP, 0) != IP_VERSION(11, 0, 4)) {
		if (amdgpu_ras_reset_error_status(obj->adev, info.head.block))
			dev_warn(obj->adev->dev, "Failed to reset error counter and error status");
	}

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
		if (strcmp(name, ras_block_string[i]) == 0)
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
	/* default value is 0 if the mask is not set by user */
	u32 instance_mask = 0;

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
			if (sscanf(str, "%*s %*s %*s 0x%x 0x%llx 0x%llx 0x%x",
				   &sub_block, &address, &value, &instance_mask) != 4 &&
			    sscanf(str, "%*s %*s %*s %u %llu %llu %u",
				   &sub_block, &address, &value, &instance_mask) != 4 &&
				sscanf(str, "%*s %*s %*s 0x%x 0x%llx 0x%llx",
				   &sub_block, &address, &value) != 3 &&
			    sscanf(str, "%*s %*s %*s %u %llu %llu",
				   &sub_block, &address, &value) != 3)
				return -EINVAL;
			data->head.sub_block_index = sub_block;
			data->inject.address = address;
			data->inject.value = value;
			data->inject.instance_mask = instance_mask;
		}
	} else {
		if (size < sizeof(*data))
			return -EINVAL;

		if (copy_from_user(data, buf, sizeof(*data)))
			return -EINVAL;
	}

	return 0;
}

static void amdgpu_ras_instance_mask_check(struct amdgpu_device *adev,
				struct ras_debug_if *data)
{
	int num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;
	uint32_t mask, inst_mask = data->inject.instance_mask;

	/* no need to set instance mask if there is only one instance */
	if (num_xcc <= 1 && inst_mask) {
		data->inject.instance_mask = 0;
		dev_dbg(adev->dev,
			"RAS inject mask(0x%x) isn't supported and force it to 0.\n",
			inst_mask);

		return;
	}

	switch (data->head.block) {
	case AMDGPU_RAS_BLOCK__GFX:
		mask = GENMASK(num_xcc - 1, 0);
		break;
	case AMDGPU_RAS_BLOCK__SDMA:
		mask = GENMASK(adev->sdma.num_instances - 1, 0);
		break;
	case AMDGPU_RAS_BLOCK__VCN:
	case AMDGPU_RAS_BLOCK__JPEG:
		mask = GENMASK(adev->vcn.num_vcn_inst - 1, 0);
		break;
	default:
		mask = inst_mask;
		break;
	}

	/* remove invalid bits in instance mask */
	data->inject.instance_mask &= mask;
	if (inst_mask != data->inject.instance_mask)
		dev_dbg(adev->dev,
			"Adjust RAS inject mask 0x%x to 0x%x\n",
			inst_mask, data->inject.instance_mask);
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
 * inject has three more members than head, they are address, value and mask.
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
 *	echo "inject  <block> <error> <sub-block> <address> <value> <mask>" > /sys/kernel/debug/dri/<N>/ras/ras_ctrl
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
 * The mask means instance mask, is optional, default value is 0x1.
 *
 * For instance,
 *
 * .. code-block:: bash
 *
 *	echo inject umc ue 0x0 0x0 0x0 > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *	echo inject umc ce 0 0 0 3 > /sys/kernel/debug/dri/0/ras/ras_ctrl
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
static ssize_t amdgpu_ras_debugfs_ctrl_write(struct file *f,
					     const char __user *buf,
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
		return ret;

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
		if ((data.inject.address >= adev->gmc.mc_vram_size &&
		    adev->gmc.mc_vram_size) ||
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
			dev_warn(adev->dev, "RAS WARN: inject: 0x%llx has "
				 "already been marked as bad!\n",
				 data.inject.address);
			break;
		}

		amdgpu_ras_instance_mask_check(adev, &data);

		/* data.inject.address is offset instead of absolute gpu address */
		ret = amdgpu_ras_error_inject(adev, &data.inject);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

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
static ssize_t amdgpu_ras_debugfs_eeprom_write(struct file *f,
					       const char __user *buf,
					       size_t size, loff_t *pos)
{
	struct amdgpu_device *adev =
		(struct amdgpu_device *)file_inode(f)->i_private;
	int ret;

	ret = amdgpu_ras_eeprom_reset_table(
		&(amdgpu_ras_get_context(adev)->eeprom_control));

	if (!ret) {
		/* Something was written to EEPROM.
		 */
		amdgpu_ras_get_context(adev)->flags = RAS_DEFAULT_FLAGS;
		return size;
	} else {
		return ret;
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

	if (amdgpu_ip_version(obj->adev, MP0_HWIP, 0) != IP_VERSION(11, 0, 2) &&
	    amdgpu_ip_version(obj->adev, MP0_HWIP, 0) != IP_VERSION(11, 0, 4)) {
		if (amdgpu_ras_reset_error_status(obj->adev, info.head.block))
			dev_warn(obj->adev->dev, "Failed to reset error counter and error status");
	}

	return sysfs_emit(buf, "%s: %lu\n%s: %lu\n", "ue", info.ue_count,
			  "ce", info.ce_count);
}

/* obj begin */

#define get_obj(obj) do { (obj)->use++; } while (0)
#define alive_obj(obj) ((obj)->use)

static inline void put_obj(struct ras_manager *obj)
{
	if (obj && (--obj->use == 0)) {
		list_del(&obj->node);
		amdgpu_ras_error_data_fini(&obj->err_data);
	}

	if (obj && (obj->use < 0))
		DRM_ERROR("RAS ERROR: Unbalance obj(%s) use\n", get_ras_block_str(&obj->head));
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

	if (head->block == AMDGPU_RAS_BLOCK__MCA) {
		if (head->sub_block_index >= AMDGPU_RAS_MCA_BLOCK__LAST)
			return NULL;

		obj = &con->objs[AMDGPU_RAS_BLOCK__LAST + head->sub_block_index];
	} else
		obj = &con->objs[head->block];

	/* already exist. return obj? */
	if (alive_obj(obj))
		return NULL;

	if (amdgpu_ras_error_data_init(&obj->err_data))
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

		if (head->block == AMDGPU_RAS_BLOCK__MCA) {
			if (head->sub_block_index >= AMDGPU_RAS_MCA_BLOCK__LAST)
				return NULL;

			obj = &con->objs[AMDGPU_RAS_BLOCK__LAST + head->sub_block_index];
		} else
			obj = &con->objs[head->block];

		if (alive_obj(obj))
			return obj;
	} else {
		for (i = 0; i < AMDGPU_RAS_BLOCK_COUNT + AMDGPU_RAS_MCA_BLOCK_COUNT; i++) {
			obj = &con->objs[i];
			if (alive_obj(obj))
				return obj;
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

	/* For non-gfx ip, do not enable ras feature if it is not allowed */
	/* For gfx ip, regardless of feature support status, */
	/* Force issue enable or disable ras feature commands */
	if (head->block != AMDGPU_RAS_BLOCK__GFX &&
	    !amdgpu_ras_is_feature_allowed(adev, head))
		return 0;

	/* Only enable gfx ras feature from host side */
	if (head->block == AMDGPU_RAS_BLOCK__GFX &&
	    !amdgpu_sriov_vf(adev) &&
	    !amdgpu_ras_intr_triggered()) {
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

		ret = psp_ras_enable_features(&adev->psp, info, enable);
		if (ret) {
			dev_err(adev->dev, "ras %s %s failed poison:%d ret:%d\n",
				enable ? "enable":"disable",
				get_ras_block_str(head),
				amdgpu_ras_is_poison_mode_supported(adev), ret);
			kfree(info);
			return ret;
		}

		kfree(info);
	}

	/* setup the obj */
	__amdgpu_ras_feature_enable(adev, head, enable);

	return 0;
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
						get_ras_block_str(head));
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
	int i;
	const enum amdgpu_ras_error_type default_ras_type = AMDGPU_RAS_ERROR__NONE;

	for (i = 0; i < AMDGPU_RAS_BLOCK_COUNT; i++) {
		struct ras_common_if head = {
			.block = i,
			.type = default_ras_type,
			.sub_block_index = 0,
		};

		if (i == AMDGPU_RAS_BLOCK__MCA)
			continue;

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

	for (i = 0; i < AMDGPU_RAS_MCA_BLOCK_COUNT; i++) {
		struct ras_common_if head = {
			.block = AMDGPU_RAS_BLOCK__MCA,
			.type = default_ras_type,
			.sub_block_index = i,
		};

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

static int amdgpu_ras_block_match_default(struct amdgpu_ras_block_object *block_obj,
		enum amdgpu_ras_block block)
{
	if (!block_obj)
		return -EINVAL;

	if (block_obj->ras_comm.block == block)
		return 0;

	return -EINVAL;
}

static struct amdgpu_ras_block_object *amdgpu_ras_get_ras_block(struct amdgpu_device *adev,
					enum amdgpu_ras_block block, uint32_t sub_block_index)
{
	struct amdgpu_ras_block_list *node, *tmp;
	struct amdgpu_ras_block_object *obj;

	if (block >= AMDGPU_RAS_BLOCK__LAST)
		return NULL;

	list_for_each_entry_safe(node, tmp, &adev->ras_list, node) {
		if (!node->ras_obj) {
			dev_warn(adev->dev, "Warning: abnormal ras list node.\n");
			continue;
		}

		obj = node->ras_obj;
		if (obj->ras_block_match) {
			if (obj->ras_block_match(obj, block, sub_block_index) == 0)
				return obj;
		} else {
			if (amdgpu_ras_block_match_default(obj, block) == 0)
				return obj;
		}
	}

	return NULL;
}

static void amdgpu_ras_get_ecc_info(struct amdgpu_device *adev, struct ras_err_data *err_data)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	int ret = 0;

	/*
	 * choosing right query method according to
	 * whether smu support query error information
	 */
	ret = amdgpu_dpm_get_ecc_info(adev, (void *)&(ras->umc_ecc));
	if (ret == -EOPNOTSUPP) {
		if (adev->umc.ras && adev->umc.ras->ras_block.hw_ops &&
			adev->umc.ras->ras_block.hw_ops->query_ras_error_count)
			adev->umc.ras->ras_block.hw_ops->query_ras_error_count(adev, err_data);

		/* umc query_ras_error_address is also responsible for clearing
		 * error status
		 */
		if (adev->umc.ras && adev->umc.ras->ras_block.hw_ops &&
		    adev->umc.ras->ras_block.hw_ops->query_ras_error_address)
			adev->umc.ras->ras_block.hw_ops->query_ras_error_address(adev, err_data);
	} else if (!ret) {
		if (adev->umc.ras &&
			adev->umc.ras->ecc_info_query_ras_error_count)
			adev->umc.ras->ecc_info_query_ras_error_count(adev, err_data);

		if (adev->umc.ras &&
			adev->umc.ras->ecc_info_query_ras_error_address)
			adev->umc.ras->ecc_info_query_ras_error_address(adev, err_data);
	}
}

static void amdgpu_ras_error_print_error_data(struct amdgpu_device *adev,
					      struct ras_manager *ras_mgr,
					      struct ras_err_data *err_data,
					      const char *blk_name,
					      bool is_ue)
{
	struct amdgpu_smuio_mcm_config_info *mcm_info;
	struct ras_err_node *err_node;
	struct ras_err_info *err_info;

	if (is_ue) {
		for_each_ras_error(err_node, err_data) {
			err_info = &err_node->err_info;
			mcm_info = &err_info->mcm_info;
			if (err_info->ue_count) {
				dev_info(adev->dev, "socket: %d, die: %d, "
					 "%lld new uncorrectable hardware errors detected in %s block\n",
					 mcm_info->socket_id,
					 mcm_info->die_id,
					 err_info->ue_count,
					 blk_name);
			}
		}

		for_each_ras_error(err_node, &ras_mgr->err_data) {
			err_info = &err_node->err_info;
			mcm_info = &err_info->mcm_info;
			dev_info(adev->dev, "socket: %d, die: %d, "
				 "%lld uncorrectable hardware errors detected in total in %s block\n",
				 mcm_info->socket_id, mcm_info->die_id, err_info->ue_count, blk_name);
		}

	} else {
		for_each_ras_error(err_node, err_data) {
			err_info = &err_node->err_info;
			mcm_info = &err_info->mcm_info;
			if (err_info->ce_count) {
				dev_info(adev->dev, "socket: %d, die: %d, "
					 "%lld new correctable hardware errors detected in %s block, "
					 "no user action is needed\n",
					 mcm_info->socket_id,
					 mcm_info->die_id,
					 err_info->ce_count,
					 blk_name);
			}
		}

		for_each_ras_error(err_node, &ras_mgr->err_data) {
			err_info = &err_node->err_info;
			mcm_info = &err_info->mcm_info;
			dev_info(adev->dev, "socket: %d, die: %d, "
				 "%lld correctable hardware errors detected in total in %s block, "
				 "no user action is needed\n",
				 mcm_info->socket_id, mcm_info->die_id, err_info->ce_count, blk_name);
		}
	}
}

static inline bool err_data_has_source_info(struct ras_err_data *data)
{
	return !list_empty(&data->err_node_list);
}

static void amdgpu_ras_error_generate_report(struct amdgpu_device *adev,
					     struct ras_query_if *query_if,
					     struct ras_err_data *err_data)
{
	struct ras_manager *ras_mgr = amdgpu_ras_find_obj(adev, &query_if->head);
	const char *blk_name = get_ras_block_str(&query_if->head);

	if (err_data->ce_count) {
		if (err_data_has_source_info(err_data)) {
			amdgpu_ras_error_print_error_data(adev, ras_mgr, err_data, blk_name, false);
		} else if (!adev->aid_mask &&
			   adev->smuio.funcs &&
			   adev->smuio.funcs->get_socket_id &&
			   adev->smuio.funcs->get_die_id) {
			dev_info(adev->dev, "socket: %d, die: %d "
				 "%ld correctable hardware errors "
				 "detected in %s block, no user "
				 "action is needed.\n",
				 adev->smuio.funcs->get_socket_id(adev),
				 adev->smuio.funcs->get_die_id(adev),
				 ras_mgr->err_data.ce_count,
				 blk_name);
		} else {
			dev_info(adev->dev, "%ld correctable hardware errors "
				 "detected in %s block, no user "
				 "action is needed.\n",
				 ras_mgr->err_data.ce_count,
				 blk_name);
		}
	}

	if (err_data->ue_count) {
		if (err_data_has_source_info(err_data)) {
			amdgpu_ras_error_print_error_data(adev, ras_mgr, err_data, blk_name, true);
		} else if (!adev->aid_mask &&
			   adev->smuio.funcs &&
			   adev->smuio.funcs->get_socket_id &&
			   adev->smuio.funcs->get_die_id) {
			dev_info(adev->dev, "socket: %d, die: %d "
				 "%ld uncorrectable hardware errors "
				 "detected in %s block\n",
				 adev->smuio.funcs->get_socket_id(adev),
				 adev->smuio.funcs->get_die_id(adev),
				 ras_mgr->err_data.ue_count,
				 blk_name);
		} else {
			dev_info(adev->dev, "%ld uncorrectable hardware errors "
				 "detected in %s block\n",
				 ras_mgr->err_data.ue_count,
				 blk_name);
		}
	}

}

static void amdgpu_rasmgr_error_data_statistic_update(struct ras_manager *obj, struct ras_err_data *err_data)
{
	struct ras_err_node *err_node;
	struct ras_err_info *err_info;

	if (err_data_has_source_info(err_data)) {
		for_each_ras_error(err_node, err_data) {
			err_info = &err_node->err_info;

			amdgpu_ras_error_statistic_ce_count(&obj->err_data, &err_info->mcm_info, err_info->ce_count);
			amdgpu_ras_error_statistic_ue_count(&obj->err_data, &err_info->mcm_info, err_info->ue_count);
		}
	} else {
		/* for legacy asic path which doesn't has error source info */
		obj->err_data.ue_count += err_data->ue_count;
		obj->err_data.ce_count += err_data->ce_count;
	}
}

static int amdgpu_ras_query_error_status_helper(struct amdgpu_device *adev,
						struct ras_query_if *info,
						struct ras_err_data *err_data,
						unsigned int error_query_mode)
{
	enum amdgpu_ras_block blk = info ? info->head.block : AMDGPU_RAS_BLOCK_COUNT;
	struct amdgpu_ras_block_object *block_obj = NULL;

	if (error_query_mode == AMDGPU_RAS_INVALID_ERROR_QUERY)
		return -EINVAL;

	if (error_query_mode == AMDGPU_RAS_DIRECT_ERROR_QUERY) {
		if (info->head.block == AMDGPU_RAS_BLOCK__UMC) {
			amdgpu_ras_get_ecc_info(adev, err_data);
		} else {
			block_obj = amdgpu_ras_get_ras_block(adev, info->head.block, 0);
			if (!block_obj || !block_obj->hw_ops) {
				dev_dbg_once(adev->dev, "%s doesn't config RAS function\n",
					     get_ras_block_str(&info->head));
				return -EINVAL;
			}

			if (block_obj->hw_ops->query_ras_error_count)
				block_obj->hw_ops->query_ras_error_count(adev, err_data);

			if ((info->head.block == AMDGPU_RAS_BLOCK__SDMA) ||
			    (info->head.block == AMDGPU_RAS_BLOCK__GFX) ||
			    (info->head.block == AMDGPU_RAS_BLOCK__MMHUB)) {
				if (block_obj->hw_ops->query_ras_error_status)
					block_obj->hw_ops->query_ras_error_status(adev);
			}
		}
	} else {
		/* FIXME: add code to check return value later */
		amdgpu_mca_smu_log_ras_error(adev, blk, AMDGPU_MCA_ERROR_TYPE_UE, err_data);
		amdgpu_mca_smu_log_ras_error(adev, blk, AMDGPU_MCA_ERROR_TYPE_CE, err_data);
	}

	return 0;
}

/* query/inject/cure begin */
int amdgpu_ras_query_error_status(struct amdgpu_device *adev, struct ras_query_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ras_err_data err_data;
	unsigned int error_query_mode;
	int ret;

	if (!obj)
		return -EINVAL;

	ret = amdgpu_ras_error_data_init(&err_data);
	if (ret)
		return ret;

	if (!amdgpu_ras_get_error_query_mode(adev, &error_query_mode))
		return -EINVAL;

	ret = amdgpu_ras_query_error_status_helper(adev, info,
						   &err_data,
						   error_query_mode);
	if (ret)
		goto out_fini_err_data;

	amdgpu_rasmgr_error_data_statistic_update(obj, &err_data);

	info->ue_count = obj->err_data.ue_count;
	info->ce_count = obj->err_data.ce_count;

	amdgpu_ras_error_generate_report(adev, info, &err_data);

out_fini_err_data:
	amdgpu_ras_error_data_fini(&err_data);

	return ret;
}

int amdgpu_ras_reset_error_count(struct amdgpu_device *adev,
		enum amdgpu_ras_block block)
{
	struct amdgpu_ras_block_object *block_obj = amdgpu_ras_get_ras_block(adev, block, 0);
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;
	struct amdgpu_hive_info *hive;
	int hive_ras_recovery = 0;

	if (!block_obj || !block_obj->hw_ops) {
		dev_dbg_once(adev->dev, "%s doesn't config RAS function\n",
				ras_block_str(block));
		return -EOPNOTSUPP;
	}

	if (!amdgpu_ras_is_supported(adev, block) ||
	    !amdgpu_ras_get_mca_debug_mode(adev))
		return -EOPNOTSUPP;

	hive = amdgpu_get_xgmi_hive(adev);
	if (hive) {
		hive_ras_recovery = atomic_read(&hive->ras_recovery);
		amdgpu_put_xgmi_hive(hive);
	}

	/* skip ras error reset in gpu reset */
	if ((amdgpu_in_reset(adev) || atomic_read(&ras->in_recovery) ||
	    hive_ras_recovery) &&
	    mca_funcs && mca_funcs->mca_set_debug_mode)
		return -EOPNOTSUPP;

	if (block_obj->hw_ops->reset_ras_error_count)
		block_obj->hw_ops->reset_ras_error_count(adev);

	return 0;
}

int amdgpu_ras_reset_error_status(struct amdgpu_device *adev,
		enum amdgpu_ras_block block)
{
	struct amdgpu_ras_block_object *block_obj = amdgpu_ras_get_ras_block(adev, block, 0);

	if (amdgpu_ras_reset_error_count(adev, block) == -EOPNOTSUPP)
		return 0;

	if ((block == AMDGPU_RAS_BLOCK__GFX) ||
	    (block == AMDGPU_RAS_BLOCK__MMHUB)) {
		if (block_obj->hw_ops->reset_ras_error_status)
			block_obj->hw_ops->reset_ras_error_status(adev);
	}

	return 0;
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
	int ret = -EINVAL;
	struct amdgpu_ras_block_object *block_obj = amdgpu_ras_get_ras_block(adev,
							info->head.block,
							info->head.sub_block_index);

	/* inject on guest isn't allowed, return success directly */
	if (amdgpu_sriov_vf(adev))
		return 0;

	if (!obj)
		return -EINVAL;

	if (!block_obj || !block_obj->hw_ops)	{
		dev_dbg_once(adev->dev, "%s doesn't config RAS function\n",
			     get_ras_block_str(&info->head));
		return -EINVAL;
	}

	/* Calculate XGMI relative offset */
	if (adev->gmc.xgmi.num_physical_nodes > 1 &&
	    info->head.block != AMDGPU_RAS_BLOCK__GFX) {
		block_info.address =
			amdgpu_xgmi_get_relative_phy_addr(adev,
							  block_info.address);
	}

	if (block_obj->hw_ops->ras_error_inject) {
		if (info->head.block == AMDGPU_RAS_BLOCK__GFX)
			ret = block_obj->hw_ops->ras_error_inject(adev, info, info->instance_mask);
		else /* Special ras_error_inject is defined (e.g: xgmi) */
			ret = block_obj->hw_ops->ras_error_inject(adev, &block_info,
						info->instance_mask);
	} else {
		/* default path */
		ret = psp_ras_trigger_error(&adev->psp, &block_info, info->instance_mask);
	}

	if (ret)
		dev_err(adev->dev, "ras inject %s failed %d\n",
			get_ras_block_str(&info->head), ret);

	return ret;
}

/**
 * amdgpu_ras_query_error_count_helper -- Get error counter for specific IP
 * @adev: pointer to AMD GPU device
 * @ce_count: pointer to an integer to be set to the count of correctible errors.
 * @ue_count: pointer to an integer to be set to the count of uncorrectible errors.
 * @query_info: pointer to ras_query_if
 *
 * Return 0 for query success or do nothing, otherwise return an error
 * on failures
 */
static int amdgpu_ras_query_error_count_helper(struct amdgpu_device *adev,
					       unsigned long *ce_count,
					       unsigned long *ue_count,
					       struct ras_query_if *query_info)
{
	int ret;

	if (!query_info)
		/* do nothing if query_info is not specified */
		return 0;

	ret = amdgpu_ras_query_error_status(adev, query_info);
	if (ret)
		return ret;

	*ce_count += query_info->ce_count;
	*ue_count += query_info->ue_count;

	/* some hardware/IP supports read to clear
	 * no need to explictly reset the err status after the query call */
	if (amdgpu_ip_version(adev, MP0_HWIP, 0) != IP_VERSION(11, 0, 2) &&
	    amdgpu_ip_version(adev, MP0_HWIP, 0) != IP_VERSION(11, 0, 4)) {
		if (amdgpu_ras_reset_error_status(adev, query_info->head.block))
			dev_warn(adev->dev,
				 "Failed to reset error counter and error status\n");
	}

	return 0;
}

/**
 * amdgpu_ras_query_error_count -- Get error counts of all IPs or specific IP
 * @adev: pointer to AMD GPU device
 * @ce_count: pointer to an integer to be set to the count of correctible errors.
 * @ue_count: pointer to an integer to be set to the count of uncorrectible
 * errors.
 * @query_info: pointer to ras_query_if if the query request is only for
 * specific ip block; if info is NULL, then the qurey request is for
 * all the ip blocks that support query ras error counters/status
 *
 * If set, @ce_count or @ue_count, count and return the corresponding
 * error counts in those integer pointers. Return 0 if the device
 * supports RAS. Return -EOPNOTSUPP if the device doesn't support RAS.
 */
int amdgpu_ras_query_error_count(struct amdgpu_device *adev,
				 unsigned long *ce_count,
				 unsigned long *ue_count,
				 struct ras_query_if *query_info)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;
	unsigned long ce, ue;
	int ret;

	if (!adev->ras_enabled || !con)
		return -EOPNOTSUPP;

	/* Don't count since no reporting.
	 */
	if (!ce_count && !ue_count)
		return 0;

	ce = 0;
	ue = 0;
	if (!query_info) {
		/* query all the ip blocks that support ras query interface */
		list_for_each_entry(obj, &con->head, node) {
			struct ras_query_if info = {
				.head = obj->head,
			};

			ret = amdgpu_ras_query_error_count_helper(adev, &ce, &ue, &info);
		}
	} else {
		/* query specific ip block */
		ret = amdgpu_ras_query_error_count_helper(adev, &ce, &ue, query_info);
	}

	if (ret)
		return ret;

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

	return sysfs_emit(buf, "feature mask: 0x%x\n", con->features);
}

static ssize_t amdgpu_ras_sysfs_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct amdgpu_ras *con =
		container_of(attr, struct amdgpu_ras, version_attr);
	return sysfs_emit(buf, "table version: 0x%x\n", con->eeprom_control.tbl_hdr.version);
}

static ssize_t amdgpu_ras_sysfs_schema_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct amdgpu_ras *con =
		container_of(attr, struct amdgpu_ras, schema_attr);
	return sysfs_emit(buf, "schema: 0x%x\n", con->schema);
}

static void amdgpu_ras_sysfs_remove_bad_page_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (adev->dev->kobj.sd)
		sysfs_remove_file_from_group(&adev->dev->kobj,
				&con->badpages_attr.attr,
				RAS_FS_NAME);
}

static int amdgpu_ras_sysfs_remove_dev_attr_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct attribute *attrs[] = {
		&con->features_attr.attr,
		&con->version_attr.attr,
		&con->schema_attr.attr,
		NULL
	};
	struct attribute_group group = {
		.name = RAS_FS_NAME,
		.attrs = attrs,
	};

	if (adev->dev->kobj.sd)
		sysfs_remove_group(&adev->dev->kobj, &group);

	return 0;
}

int amdgpu_ras_sysfs_create(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, head);

	if (!obj || obj->attr_inuse)
		return -EINVAL;

	get_obj(obj);

	snprintf(obj->fs_data.sysfs_name, sizeof(obj->fs_data.sysfs_name),
		"%s_err_count", head->name);

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

	if (adev->dev->kobj.sd)
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

	amdgpu_ras_sysfs_remove_dev_attr_node(adev);

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
	struct amdgpu_ras_eeprom_control *eeprom = &con->eeprom_control;
	struct drm_minor  *minor = adev_to_drm(adev)->primary;
	struct dentry     *dir;

	dir = debugfs_create_dir(RAS_FS_NAME, minor->debugfs_root);
	debugfs_create_file("ras_ctrl", S_IWUGO | S_IRUGO, dir, adev,
			    &amdgpu_ras_debugfs_ctrl_ops);
	debugfs_create_file("ras_eeprom_reset", S_IWUGO | S_IRUGO, dir, adev,
			    &amdgpu_ras_debugfs_eeprom_ops);
	debugfs_create_u32("bad_page_cnt_threshold", 0444, dir,
			   &con->bad_page_cnt_threshold);
	debugfs_create_u32("ras_num_recs", 0444, dir, &eeprom->ras_num_recs);
	debugfs_create_x32("ras_hw_enabled", 0444, dir, &adev->ras_hw_enabled);
	debugfs_create_x32("ras_enabled", 0444, dir, &adev->ras_enabled);
	debugfs_create_file("ras_eeprom_size", S_IRUGO, dir, adev,
			    &amdgpu_ras_debugfs_eeprom_size_ops);
	con->de_ras_eeprom_table = debugfs_create_file("ras_eeprom_table",
						       S_IRUGO, dir, adev,
						       &amdgpu_ras_debugfs_eeprom_table_ops);
	amdgpu_ras_debugfs_set_ret_size(&con->eeprom_control);

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
					get_ras_block_str(&obj->head));
			fs_info.head = obj->head;
			amdgpu_ras_debugfs_create(adev, &fs_info, dir);
		}
	}

	amdgpu_mca_smu_debugfs_init(adev, dir);
}

/* debugfs end */

/* ras fs */
static BIN_ATTR(gpu_vram_bad_pages, S_IRUGO,
		amdgpu_ras_sysfs_badpages_read, NULL, 0);
static DEVICE_ATTR(features, S_IRUGO,
		amdgpu_ras_sysfs_features_read, NULL);
static DEVICE_ATTR(version, 0444,
		amdgpu_ras_sysfs_version_show, NULL);
static DEVICE_ATTR(schema, 0444,
		amdgpu_ras_sysfs_schema_show, NULL);
static int amdgpu_ras_fs_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct attribute_group group = {
		.name = RAS_FS_NAME,
	};
	struct attribute *attrs[] = {
		&con->features_attr.attr,
		&con->version_attr.attr,
		&con->schema_attr.attr,
		NULL
	};
	struct bin_attribute *bin_attrs[] = {
		NULL,
		NULL,
	};
	int r;

	group.attrs = attrs;

	/* add features entry */
	con->features_attr = dev_attr_features;
	sysfs_attr_init(attrs[0]);

	/* add version entry */
	con->version_attr = dev_attr_version;
	sysfs_attr_init(attrs[1]);

	/* add schema entry */
	con->schema_attr = dev_attr_schema;
	sysfs_attr_init(attrs[2]);

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

/* For the hardware that cannot enable bif ring for both ras_controller_irq
 * and ras_err_evnet_athub_irq ih cookies, the driver has to poll status
 * register to check whether the interrupt is triggered or not, and properly
 * ack the interrupt if it is there
 */
void amdgpu_ras_interrupt_fatal_error_handler(struct amdgpu_device *adev)
{
	/* Fatal error events are handled on host side */
	if (amdgpu_sriov_vf(adev))
		return;

	if (adev->nbio.ras &&
	    adev->nbio.ras->handle_ras_controller_intr_no_bifring)
		adev->nbio.ras->handle_ras_controller_intr_no_bifring(adev);

	if (adev->nbio.ras &&
	    adev->nbio.ras->handle_ras_err_event_athub_intr_no_bifring)
		adev->nbio.ras->handle_ras_err_event_athub_intr_no_bifring(adev);
}

static void amdgpu_ras_interrupt_poison_consumption_handler(struct ras_manager *obj,
				struct amdgpu_iv_entry *entry)
{
	bool poison_stat = false;
	struct amdgpu_device *adev = obj->adev;
	struct amdgpu_ras_block_object *block_obj =
		amdgpu_ras_get_ras_block(adev, obj->head.block, 0);

	if (!block_obj)
		return;

	/* both query_poison_status and handle_poison_consumption are optional,
	 * but at least one of them should be implemented if we need poison
	 * consumption handler
	 */
	if (block_obj->hw_ops && block_obj->hw_ops->query_poison_status) {
		poison_stat = block_obj->hw_ops->query_poison_status(adev);
		if (!poison_stat) {
			/* Not poison consumption interrupt, no need to handle it */
			dev_info(adev->dev, "No RAS poison status in %s poison IH.\n",
					block_obj->ras_comm.name);

			return;
		}
	}

	amdgpu_umc_poison_handler(adev, false);

	if (block_obj->hw_ops && block_obj->hw_ops->handle_poison_consumption)
		poison_stat = block_obj->hw_ops->handle_poison_consumption(adev);

	/* gpu reset is fallback for failed and default cases */
	if (poison_stat) {
		dev_info(adev->dev, "GPU reset for %s RAS poison consumption is issued!\n",
				block_obj->ras_comm.name);
		amdgpu_ras_reset_gpu(adev);
	} else {
		amdgpu_gfx_poison_consumption_handler(adev, entry);
	}
}

static void amdgpu_ras_interrupt_poison_creation_handler(struct ras_manager *obj,
				struct amdgpu_iv_entry *entry)
{
	dev_info(obj->adev->dev,
		"Poison is created, no user action is needed.\n");
}

static void amdgpu_ras_interrupt_umc_handler(struct ras_manager *obj,
				struct amdgpu_iv_entry *entry)
{
	struct ras_ih_data *data = &obj->ih_data;
	struct ras_err_data err_data;
	int ret;

	if (!data->cb)
		return;

	ret = amdgpu_ras_error_data_init(&err_data);
	if (ret)
		return;

	/* Let IP handle its data, maybe we need get the output
	 * from the callback to update the error type/count, etc
	 */
	ret = data->cb(obj->adev, &err_data, entry);
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

	amdgpu_ras_error_data_fini(&err_data);
}

static void amdgpu_ras_interrupt_handler(struct ras_manager *obj)
{
	struct ras_ih_data *data = &obj->ih_data;
	struct amdgpu_iv_entry entry;

	while (data->rptr != data->wptr) {
		rmb();
		memcpy(&entry, &data->ring[data->rptr],
				data->element_size);

		wmb();
		data->rptr = (data->aligned_element_size +
				data->rptr) % data->ring_size;

		if (amdgpu_ras_is_poison_mode_supported(obj->adev)) {
			if (obj->head.block == AMDGPU_RAS_BLOCK__UMC)
				amdgpu_ras_interrupt_poison_creation_handler(obj, &entry);
			else
				amdgpu_ras_interrupt_poison_consumption_handler(obj, &entry);
		} else {
			if (obj->head.block == AMDGPU_RAS_BLOCK__UMC)
				amdgpu_ras_interrupt_umc_handler(obj, &entry);
			else
				dev_warn(obj->adev->dev,
					"No RAS interrupt handler for non-UMC block with poison disabled.\n");
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
		struct ras_common_if *head)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, head);
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
		struct ras_common_if *head)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, head);
	struct ras_ih_data *data;
	struct amdgpu_ras_block_object *ras_obj;

	if (!obj) {
		/* in case we registe the IH before enable ras feature */
		obj = amdgpu_ras_create_obj(adev, head);
		if (!obj)
			return -EINVAL;
	} else
		get_obj(obj);

	ras_obj = container_of(head, struct amdgpu_ras_block_object, ras_comm);

	data = &obj->ih_data;
	/* add the callback.etc */
	*data = (struct ras_ih_data) {
		.inuse = 0,
		.cb = ras_obj->ras_cb,
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
		amdgpu_ras_interrupt_remove_handler(adev, &obj->head);
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

		/*
		 * this is a workaround for aldebaran, skip send msg to
		 * smu to get ecc_info table due to smu handle get ecc
		 * info table failed temporarily.
		 * should be removed until smu fix handle ecc_info table.
		 */
		if ((info.head.block == AMDGPU_RAS_BLOCK__UMC) &&
		    (amdgpu_ip_version(adev, MP1_HWIP, 0) ==
		     IP_VERSION(13, 0, 2)))
			continue;

		amdgpu_ras_query_error_status(adev, &info);

		if (amdgpu_ip_version(adev, MP0_HWIP, 0) !=
			    IP_VERSION(11, 0, 2) &&
		    amdgpu_ip_version(adev, MP0_HWIP, 0) !=
			    IP_VERSION(11, 0, 4) &&
		    amdgpu_ip_version(adev, MP0_HWIP, 0) !=
			    IP_VERSION(13, 0, 0)) {
			if (amdgpu_ras_reset_error_status(adev, info.head.block))
				dev_warn(adev->dev, "Failed to reset error counter and error status");
		}
	}
}

/* Parse RdRspStatus and WrRspStatus */
static void amdgpu_ras_error_status_query(struct amdgpu_device *adev,
					  struct ras_query_if *info)
{
	struct amdgpu_ras_block_object *block_obj;
	/*
	 * Only two block need to query read/write
	 * RspStatus at current state
	 */
	if ((info->head.block != AMDGPU_RAS_BLOCK__GFX) &&
		(info->head.block != AMDGPU_RAS_BLOCK__MMHUB))
		return;

	block_obj = amdgpu_ras_get_ras_block(adev,
					info->head.block,
					info->head.sub_block_index);

	if (!block_obj || !block_obj->hw_ops) {
		dev_dbg_once(adev->dev, "%s doesn't config RAS function\n",
			     get_ras_block_str(&info->head));
		return;
	}

	if (block_obj->hw_ops->query_ras_error_status)
		block_obj->hw_ops->query_ras_error_status(adev);

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
		status = amdgpu_vram_mgr_query_page_status(&adev->mman.vram_mgr,
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
	struct amdgpu_hive_info *hive = amdgpu_get_xgmi_hive(adev);

	if (hive)
		atomic_set(&hive->ras_recovery, 1);
	if (!ras->disable_ras_err_cnt_harvest) {

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

	}

	if (amdgpu_device_should_recover_gpu(ras->adev)) {
		struct amdgpu_reset_context reset_context;
		memset(&reset_context, 0, sizeof(reset_context));

		reset_context.method = AMD_RESET_METHOD_NONE;
		reset_context.reset_req_dev = adev;

		/* Perform full reset in fatal error mode */
		if (!amdgpu_ras_is_poison_mode_supported(ras->adev))
			set_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);
		else {
			clear_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);

			if (ras->gpu_reset_flags & AMDGPU_RAS_GPU_RESET_MODE2_RESET) {
				ras->gpu_reset_flags &= ~AMDGPU_RAS_GPU_RESET_MODE2_RESET;
				reset_context.method = AMD_RESET_METHOD_MODE2;
			}

			/* Fatal error occurs in poison mode, mode1 reset is used to
			 * recover gpu.
			 */
			if (ras->gpu_reset_flags & AMDGPU_RAS_GPU_RESET_MODE1_RESET) {
				ras->gpu_reset_flags &= ~AMDGPU_RAS_GPU_RESET_MODE1_RESET;
				set_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);

				psp_fatal_error_recovery_quirk(&adev->psp);
			}
		}

		amdgpu_device_gpu_recover(ras->adev, NULL, &reset_context);
	}
	atomic_set(&ras->in_recovery, 0);
	if (hive) {
		atomic_set(&hive->ras_recovery, 0);
		amdgpu_put_xgmi_hive(hive);
	}
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

		amdgpu_vram_mgr_reserve_range(&adev->mman.vram_mgr,
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
 * new_cnt: new added UE count, excluding reserved bad pages, can be NULL
 */
int amdgpu_ras_save_bad_pages(struct amdgpu_device *adev,
		unsigned long *new_cnt)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data;
	struct amdgpu_ras_eeprom_control *control;
	int save_count;

	if (!con || !con->eh_data) {
		if (new_cnt)
			*new_cnt = 0;

		return 0;
	}

	mutex_lock(&con->recovery_lock);
	control = &con->eeprom_control;
	data = con->eh_data;
	save_count = data->count - control->ras_num_recs;
	mutex_unlock(&con->recovery_lock);

	if (new_cnt)
		*new_cnt = save_count / adev->umc.retire_unit;

	/* only new entries are saved */
	if (save_count > 0) {
		if (amdgpu_ras_eeprom_append(control,
					     &data->bps[control->ras_num_recs],
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
		&adev->psp.ras_context.ras->eeprom_control;
	struct eeprom_table_record *bps;
	int ret;

	/* no bad page record, skip eeprom access */
	if (control->ras_num_recs == 0 || amdgpu_bad_page_threshold == 0)
		return 0;

	bps = kcalloc(control->ras_num_recs, sizeof(*bps), GFP_KERNEL);
	if (!bps)
		return -ENOMEM;

	ret = amdgpu_ras_eeprom_read(control, bps, control->ras_num_recs);
	if (ret)
		dev_err(adev->dev, "Failed to load EEPROM table records!");
	else
		ret = amdgpu_ras_add_bad_pages(adev, bps, control->ras_num_recs);

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
					  uint32_t max_count)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	/*
	 * Justification of value bad_page_cnt_threshold in ras structure
	 *
	 * Generally, 0 <= amdgpu_bad_page_threshold <= max record length
	 * in eeprom or amdgpu_bad_page_threshold == -2, introduce two
	 * scenarios accordingly.
	 *
	 * Bad page retirement enablement:
	 *    - If amdgpu_bad_page_threshold = -2,
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

	if (amdgpu_bad_page_threshold < 0) {
		u64 val = adev->gmc.mc_vram_size;

		do_div(val, RAS_BAD_PAGE_COVER);
		con->bad_page_cnt_threshold = min(lower_32_bits(val),
						  max_count);
	} else {
		con->bad_page_cnt_threshold = min_t(int, max_count,
						    amdgpu_bad_page_threshold);
	}
}

int amdgpu_ras_recovery_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data **data;
	u32  max_eeprom_records_count = 0;
	bool exc_err_limit = false;
	int ret;

	if (!con || amdgpu_sriov_vf(adev))
		return 0;

	/* Allow access to RAS EEPROM via debugfs, when the ASIC
	 * supports RAS and debugfs is enabled, but when
	 * adev->ras_enabled is unset, i.e. when "ras_enable"
	 * module parameter is set to 0.
	 */
	con->adev = adev;

	if (!adev->ras_enabled)
		return 0;

	data = &con->eh_data;
	*data = kmalloc(sizeof(**data), GFP_KERNEL | __GFP_ZERO);
	if (!*data) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_init(&con->recovery_lock);
	INIT_WORK(&con->recovery_work, amdgpu_ras_do_recovery);
	atomic_set(&con->in_recovery, 0);
	con->eeprom_control.bad_channel_bitmap = 0;

	max_eeprom_records_count = amdgpu_ras_eeprom_max_record_count(&con->eeprom_control);
	amdgpu_ras_validate_threshold(adev, max_eeprom_records_count);

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

	if (con->eeprom_control.ras_num_recs) {
		ret = amdgpu_ras_load_bad_pages(adev);
		if (ret)
			goto free;

		amdgpu_dpm_send_hbm_bad_pages_num(adev, con->eeprom_control.ras_num_recs);

		if (con->update_channel_flag == true) {
			amdgpu_dpm_send_hbm_bad_channel_flag(adev, con->eeprom_control.bad_channel_bitmap);
			con->update_channel_flag = false;
		}
	}

#ifdef CONFIG_X86_MCE_AMD
	if ((adev->asic_type == CHIP_ALDEBARAN) &&
	    (adev->gmc.xgmi.connected_to_cpu))
		amdgpu_register_bad_pages_mca_notifier(adev);
#endif
	return 0;

free:
	kfree((*data)->bps);
	kfree(*data);
	con->eh_data = NULL;
out:
	dev_warn(adev->dev, "Failed to initialize ras recovery! (%d)\n", ret);

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

static bool amdgpu_ras_asic_supported(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev)) {
		switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
		case IP_VERSION(13, 0, 2):
		case IP_VERSION(13, 0, 6):
			return true;
		default:
			return false;
		}
	}

	if (adev->asic_type == CHIP_IP_DISCOVERY) {
		switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
		case IP_VERSION(13, 0, 0):
		case IP_VERSION(13, 0, 6):
		case IP_VERSION(13, 0, 10):
			return true;
		default:
			return false;
		}
	}

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

	if (strnstr(ctx->vbios_pn, "D16406",
		    sizeof(ctx->vbios_pn)) ||
		strnstr(ctx->vbios_pn, "D36002",
			sizeof(ctx->vbios_pn)))
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

	if (!amdgpu_ras_asic_supported(adev))
		return;

	if (!adev->gmc.xgmi.connected_to_cpu &&	!adev->gmc.is_app_apu) {
		if (amdgpu_atomfirmware_mem_ecc_supported(adev)) {
			dev_info(adev->dev, "MEM ECC is active.\n");
			adev->ras_hw_enabled |= (1 << AMDGPU_RAS_BLOCK__UMC |
						   1 << AMDGPU_RAS_BLOCK__DF);
		} else {
			dev_info(adev->dev, "MEM ECC is not presented.\n");
		}

		if (amdgpu_atomfirmware_sram_ecc_supported(adev)) {
			dev_info(adev->dev, "SRAM ECC is active.\n");
			if (!amdgpu_sriov_vf(adev))
				adev->ras_hw_enabled |= ~(1 << AMDGPU_RAS_BLOCK__UMC |
							    1 << AMDGPU_RAS_BLOCK__DF);
			else
				adev->ras_hw_enabled |= (1 << AMDGPU_RAS_BLOCK__PCIE_BIF |
								1 << AMDGPU_RAS_BLOCK__SDMA |
								1 << AMDGPU_RAS_BLOCK__GFX);

			/* VCN/JPEG RAS can be supported on both bare metal and
			 * SRIOV environment
			 */
			if (amdgpu_ip_version(adev, VCN_HWIP, 0) ==
				    IP_VERSION(2, 6, 0) ||
			    amdgpu_ip_version(adev, VCN_HWIP, 0) ==
				    IP_VERSION(4, 0, 0) ||
			    amdgpu_ip_version(adev, VCN_HWIP, 0) ==
				    IP_VERSION(4, 0, 3))
				adev->ras_hw_enabled |= (1 << AMDGPU_RAS_BLOCK__VCN |
							1 << AMDGPU_RAS_BLOCK__JPEG);
			else
				adev->ras_hw_enabled &= ~(1 << AMDGPU_RAS_BLOCK__VCN |
							1 << AMDGPU_RAS_BLOCK__JPEG);

			/*
			 * XGMI RAS is not supported if xgmi num physical nodes
			 * is zero
			 */
			if (!adev->gmc.xgmi.num_physical_nodes)
				adev->ras_hw_enabled &= ~(1 << AMDGPU_RAS_BLOCK__XGMI_WAFL);
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
	if (amdgpu_ras_query_error_count(adev, &ce_count, &ue_count, NULL) == 0) {
		atomic_set(&con->ras_ce_count, ce_count);
		atomic_set(&con->ras_ue_count, ue_count);
	}

	pm_runtime_mark_last_busy(dev->dev);
Out:
	pm_runtime_put_autosuspend(dev->dev);
}

static void amdgpu_ras_query_poison_mode(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	bool df_poison, umc_poison;

	/* poison setting is useless on SRIOV guest */
	if (amdgpu_sriov_vf(adev) || !con)
		return;

	/* Init poison supported flag, the default value is false */
	if (adev->gmc.xgmi.connected_to_cpu ||
	    adev->gmc.is_app_apu) {
		/* enabled by default when GPU is connected to CPU */
		con->poison_supported = true;
	} else if (adev->df.funcs &&
	    adev->df.funcs->query_ras_poison_mode &&
	    adev->umc.ras &&
	    adev->umc.ras->query_ras_poison_mode) {
		df_poison =
			adev->df.funcs->query_ras_poison_mode(adev);
		umc_poison =
			adev->umc.ras->query_ras_poison_mode(adev);

		/* Only poison is set in both DF and UMC, we can support it */
		if (df_poison && umc_poison)
			con->poison_supported = true;
		else if (df_poison != umc_poison)
			dev_warn(adev->dev,
				"Poison setting is inconsistent in DF/UMC(%d:%d)!\n",
				df_poison, umc_poison);
	}
}

static int amdgpu_get_ras_schema(struct amdgpu_device *adev)
{
	return  amdgpu_ras_is_poison_mode_supported(adev) ? AMDGPU_RAS_ERROR__POISON : 0 |
			AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE |
			AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE |
			AMDGPU_RAS_ERROR__PARITY;
}

int amdgpu_ras_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int r;

	if (con)
		return 0;

	con = kmalloc(sizeof(struct amdgpu_ras) +
			sizeof(struct ras_manager) * AMDGPU_RAS_BLOCK_COUNT +
			sizeof(struct ras_manager) * AMDGPU_RAS_MCA_BLOCK_COUNT,
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

	con->update_channel_flag = false;
	con->features = 0;
	con->schema = 0;
	INIT_LIST_HEAD(&con->head);
	/* Might need get this flag from vbios. */
	con->flags = RAS_DEFAULT_FLAGS;

	/* initialize nbio ras function ahead of any other
	 * ras functions so hardware fatal error interrupt
	 * can be enabled as early as possible */
	switch (amdgpu_ip_version(adev, NBIO_HWIP, 0)) {
	case IP_VERSION(7, 4, 0):
	case IP_VERSION(7, 4, 1):
	case IP_VERSION(7, 4, 4):
		if (!adev->gmc.xgmi.connected_to_cpu)
			adev->nbio.ras = &nbio_v7_4_ras;
		break;
	case IP_VERSION(4, 3, 0):
		if (adev->ras_hw_enabled & (1 << AMDGPU_RAS_BLOCK__DF))
			/* unlike other generation of nbio ras,
			 * nbio v4_3 only support fatal error interrupt
			 * to inform software that DF is freezed due to
			 * system fatal error event. driver should not
			 * enable nbio ras in such case. Instead,
			 * check DF RAS */
			adev->nbio.ras = &nbio_v4_3_ras;
		break;
	case IP_VERSION(7, 9, 0):
		if (!adev->gmc.is_app_apu)
			adev->nbio.ras = &nbio_v7_9_ras;
		break;
	default:
		/* nbio ras is not available */
		break;
	}

	/* nbio ras block needs to be enabled ahead of other ras blocks
	 * to handle fatal error */
	r = amdgpu_nbio_ras_sw_init(adev);
	if (r)
		return r;

	if (adev->nbio.ras &&
	    adev->nbio.ras->init_ras_controller_interrupt) {
		r = adev->nbio.ras->init_ras_controller_interrupt(adev);
		if (r)
			goto release_con;
	}

	if (adev->nbio.ras &&
	    adev->nbio.ras->init_ras_err_event_athub_interrupt) {
		r = adev->nbio.ras->init_ras_err_event_athub_interrupt(adev);
		if (r)
			goto release_con;
	}

	amdgpu_ras_query_poison_mode(adev);

	/* Get RAS schema for particular SOC */
	con->schema = amdgpu_get_ras_schema(adev);

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
	if (adev->gmc.xgmi.connected_to_cpu ||
	    adev->gmc.is_app_apu)
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

bool amdgpu_ras_is_poison_mode_supported(struct amdgpu_device *adev)
{
       struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

       if (!con)
               return false;

       return con->poison_supported;
}

/* helper function to handle common stuff in ip late init phase */
int amdgpu_ras_block_late_init(struct amdgpu_device *adev,
			 struct ras_common_if *ras_block)
{
	struct amdgpu_ras_block_object *ras_obj = NULL;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_query_if *query_info;
	unsigned long ue_count, ce_count;
	int r;

	/* disable RAS feature per IP block if it is not supported */
	if (!amdgpu_ras_is_supported(adev, ras_block->block)) {
		amdgpu_ras_feature_enable_on_boot(adev, ras_block, 0);
		return 0;
	}

	r = amdgpu_ras_feature_enable_on_boot(adev, ras_block, 1);
	if (r) {
		if (adev->in_suspend || amdgpu_in_reset(adev)) {
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

	ras_obj = container_of(ras_block, struct amdgpu_ras_block_object, ras_comm);
	if (ras_obj->ras_cb || (ras_obj->hw_ops &&
	    (ras_obj->hw_ops->query_poison_status ||
	    ras_obj->hw_ops->handle_poison_consumption))) {
		r = amdgpu_ras_interrupt_add_handler(adev, ras_block);
		if (r)
			goto cleanup;
	}

	if (ras_obj->hw_ops &&
	    (ras_obj->hw_ops->query_ras_error_count ||
	     ras_obj->hw_ops->query_ras_error_status)) {
		r = amdgpu_ras_sysfs_create(adev, ras_block);
		if (r)
			goto interrupt;

		/* Those are the cached values at init.
		 */
		query_info = kzalloc(sizeof(*query_info), GFP_KERNEL);
		if (!query_info)
			return -ENOMEM;
		memcpy(&query_info->head, ras_block, sizeof(struct ras_common_if));

		if (amdgpu_ras_query_error_count(adev, &ce_count, &ue_count, query_info) == 0) {
			atomic_set(&con->ras_ce_count, ce_count);
			atomic_set(&con->ras_ue_count, ue_count);
		}

		kfree(query_info);
	}

	return 0;

interrupt:
	if (ras_obj->ras_cb)
		amdgpu_ras_interrupt_remove_handler(adev, ras_block);
cleanup:
	amdgpu_ras_feature_enable(adev, ras_block, 0);
	return r;
}

static int amdgpu_ras_block_late_init_default(struct amdgpu_device *adev,
			 struct ras_common_if *ras_block)
{
	return amdgpu_ras_block_late_init(adev, ras_block);
}

/* helper function to remove ras fs node and interrupt handler */
void amdgpu_ras_block_late_fini(struct amdgpu_device *adev,
			  struct ras_common_if *ras_block)
{
	struct amdgpu_ras_block_object *ras_obj;
	if (!ras_block)
		return;

	amdgpu_ras_sysfs_remove(adev, ras_block);

	ras_obj = container_of(ras_block, struct amdgpu_ras_block_object, ras_comm);
	if (ras_obj->ras_cb)
		amdgpu_ras_interrupt_remove_handler(adev, ras_block);
}

static void amdgpu_ras_block_late_fini_default(struct amdgpu_device *adev,
			  struct ras_common_if *ras_block)
{
	return amdgpu_ras_block_late_fini(adev, ras_block);
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

int amdgpu_ras_late_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras_block_list *node, *tmp;
	struct amdgpu_ras_block_object *obj;
	int r;

	/* Guest side doesn't need init ras feature */
	if (amdgpu_sriov_vf(adev))
		return 0;

	/* enable MCA debug on APU device */
	amdgpu_ras_set_mca_debug_mode(adev, !!(adev->flags & AMD_IS_APU));

	list_for_each_entry_safe(node, tmp, &adev->ras_list, node) {
		if (!node->ras_obj) {
			dev_warn(adev->dev, "Warning: abnormal ras list node.\n");
			continue;
		}

		obj = node->ras_obj;
		if (obj->ras_late_init) {
			r = obj->ras_late_init(adev, &obj->ras_comm);
			if (r) {
				dev_err(adev->dev, "%s failed to execute ras_late_init! ret:%d\n",
					obj->ras_comm.name, r);
				return r;
			}
		} else
			amdgpu_ras_block_late_init_default(adev, &obj->ras_comm);
	}

	return 0;
}

/* do some fini work before IP fini as dependence */
int amdgpu_ras_pre_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!adev->ras_enabled || !con)
		return 0;


	/* Need disable ras on all IPs here before ip [hw/sw]fini */
	if (con->features)
		amdgpu_ras_disable_all_features(adev, 0);
	amdgpu_ras_recovery_fini(adev);
	return 0;
}

int amdgpu_ras_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras_block_list *ras_node, *tmp;
	struct amdgpu_ras_block_object *obj = NULL;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!adev->ras_enabled || !con)
		return 0;

	list_for_each_entry_safe(ras_node, tmp, &adev->ras_list, node) {
		if (ras_node->ras_obj) {
			obj = ras_node->ras_obj;
			if (amdgpu_ras_is_supported(adev, obj->ras_comm.block) &&
			    obj->ras_fini)
				obj->ras_fini(adev, &obj->ras_comm);
			else
				amdgpu_ras_block_late_fini_default(adev, &obj->ras_comm);
		}

		/* Clear ras blocks from ras_list and free ras block list node */
		list_del(&ras_node->node);
		kfree(ras_node);
	}

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
	if (atomic_cmpxchg(&amdgpu_ras_in_intr, 0, 1) == 0) {
		struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

		dev_info(adev->dev, "uncorrectable hardware error"
			"(ERREVENT_ATHUB_INTERRUPT) detected!\n");

		ras->gpu_reset_flags |= AMDGPU_RAS_GPU_RESET_MODE1_RESET;
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

#ifdef CONFIG_X86_MCE_AMD
static struct amdgpu_device *find_adev(uint32_t node_id)
{
	int i;
	struct amdgpu_device *adev = NULL;

	for (i = 0; i < mce_adev_list.num_gpu; i++) {
		adev = mce_adev_list.devs[i];

		if (adev && adev->gmc.xgmi.connected_to_cpu &&
		    adev->gmc.xgmi.physical_node_id == node_id)
			break;
		adev = NULL;
	}

	return adev;
}

#define GET_MCA_IPID_GPUID(m)	(((m) >> 44) & 0xF)
#define GET_UMC_INST(m)		(((m) >> 21) & 0x7)
#define GET_CHAN_INDEX(m)	((((m) >> 12) & 0x3) | (((m) >> 18) & 0x4))
#define GPU_ID_OFFSET		8

static int amdgpu_bad_page_notifier(struct notifier_block *nb,
				    unsigned long val, void *data)
{
	struct mce *m = (struct mce *)data;
	struct amdgpu_device *adev = NULL;
	uint32_t gpu_id = 0;
	uint32_t umc_inst = 0, ch_inst = 0;

	/*
	 * If the error was generated in UMC_V2, which belongs to GPU UMCs,
	 * and error occurred in DramECC (Extended error code = 0) then only
	 * process the error, else bail out.
	 */
	if (!m || !((smca_get_bank_type(m->extcpu, m->bank) == SMCA_UMC_V2) &&
		    (XEC(m->status, 0x3f) == 0x0)))
		return NOTIFY_DONE;

	/*
	 * If it is correctable error, return.
	 */
	if (mce_is_correctable(m))
		return NOTIFY_OK;

	/*
	 * GPU Id is offset by GPU_ID_OFFSET in MCA_IPID_UMC register.
	 */
	gpu_id = GET_MCA_IPID_GPUID(m->ipid) - GPU_ID_OFFSET;

	adev = find_adev(gpu_id);
	if (!adev) {
		DRM_WARN("%s: Unable to find adev for gpu_id: %d\n", __func__,
								gpu_id);
		return NOTIFY_DONE;
	}

	/*
	 * If it is uncorrectable error, then find out UMC instance and
	 * channel index.
	 */
	umc_inst = GET_UMC_INST(m->ipid);
	ch_inst = GET_CHAN_INDEX(m->ipid);

	dev_info(adev->dev, "Uncorrectable error detected in UMC inst: %d, chan_idx: %d",
			     umc_inst, ch_inst);

	if (!amdgpu_umc_page_retirement_mca(adev, m->addr, ch_inst, umc_inst))
		return NOTIFY_OK;
	else
		return NOTIFY_DONE;
}

static struct notifier_block amdgpu_bad_page_nb = {
	.notifier_call  = amdgpu_bad_page_notifier,
	.priority       = MCE_PRIO_UC,
};

static void amdgpu_register_bad_pages_mca_notifier(struct amdgpu_device *adev)
{
	/*
	 * Add the adev to the mce_adev_list.
	 * During mode2 reset, amdgpu device is temporarily
	 * removed from the mgpu_info list which can cause
	 * page retirement to fail.
	 * Use this list instead of mgpu_info to find the amdgpu
	 * device on which the UMC error was reported.
	 */
	mce_adev_list.devs[mce_adev_list.num_gpu++] = adev;

	/*
	 * Register the x86 notifier only once
	 * with MCE subsystem.
	 */
	if (notifier_registered == false) {
		mce_register_decode_chain(&amdgpu_bad_page_nb);
		notifier_registered = true;
	}
}
#endif

struct amdgpu_ras *amdgpu_ras_get_context(struct amdgpu_device *adev)
{
	if (!adev)
		return NULL;

	return adev->psp.ras_context.ras;
}

int amdgpu_ras_set_context(struct amdgpu_device *adev, struct amdgpu_ras *ras_con)
{
	if (!adev)
		return -EINVAL;

	adev->psp.ras_context.ras = ras_con;
	return 0;
}

/* check if ras is supported on block, say, sdma, gfx */
int amdgpu_ras_is_supported(struct amdgpu_device *adev,
		unsigned int block)
{
	int ret = 0;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (block >= AMDGPU_RAS_BLOCK_COUNT)
		return 0;

	ret = ras && (adev->ras_enabled & (1 << block));

	/* For the special asic with mem ecc enabled but sram ecc
	 * not enabled, even if the ras block is not supported on
	 * .ras_enabled, if the asic supports poison mode and the
	 * ras block has ras configuration, it can be considered
	 * that the ras block supports ras function.
	 */
	if (!ret &&
	    (block == AMDGPU_RAS_BLOCK__GFX ||
	     block == AMDGPU_RAS_BLOCK__SDMA ||
	     block == AMDGPU_RAS_BLOCK__VCN ||
	     block == AMDGPU_RAS_BLOCK__JPEG) &&
	    amdgpu_ras_is_poison_mode_supported(adev) &&
	    amdgpu_ras_get_ras_block(adev, block, 0))
		ret = 1;

	return ret;
}

int amdgpu_ras_reset_gpu(struct amdgpu_device *adev)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (atomic_cmpxchg(&ras->in_recovery, 0, 1) == 0)
		amdgpu_reset_domain_schedule(ras->adev->reset_domain, &ras->recovery_work);
	return 0;
}

int amdgpu_ras_set_mca_debug_mode(struct amdgpu_device *adev, bool enable)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int ret = 0;

	if (con) {
		ret = amdgpu_mca_smu_set_debug_mode(adev, enable);
		if (!ret)
			con->is_mca_debug_mode = enable;
	}

	return ret;
}

bool amdgpu_ras_get_mca_debug_mode(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;

	if (!con)
		return false;

	if (mca_funcs && mca_funcs->mca_set_debug_mode)
		return con->is_mca_debug_mode;
	else
		return true;
}

bool amdgpu_ras_get_error_query_mode(struct amdgpu_device *adev,
				     unsigned int *error_query_mode)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;

	if (!con) {
		*error_query_mode = AMDGPU_RAS_INVALID_ERROR_QUERY;
		return false;
	}

	if (mca_funcs && mca_funcs->mca_set_debug_mode)
		*error_query_mode =
			(con->is_mca_debug_mode) ? AMDGPU_RAS_DIRECT_ERROR_QUERY : AMDGPU_RAS_FIRMWARE_ERROR_QUERY;
	else
		*error_query_mode = AMDGPU_RAS_DIRECT_ERROR_QUERY;

	return true;
}

/* Register each ip ras block into amdgpu ras */
int amdgpu_ras_register_ras_block(struct amdgpu_device *adev,
		struct amdgpu_ras_block_object *ras_block_obj)
{
	struct amdgpu_ras_block_list *ras_node;
	if (!adev || !ras_block_obj)
		return -EINVAL;

	ras_node = kzalloc(sizeof(*ras_node), GFP_KERNEL);
	if (!ras_node)
		return -ENOMEM;

	INIT_LIST_HEAD(&ras_node->node);
	ras_node->ras_obj = ras_block_obj;
	list_add_tail(&ras_node->node, &adev->ras_list);

	return 0;
}

void amdgpu_ras_get_error_type_name(uint32_t err_type, char *err_type_name)
{
	if (!err_type_name)
		return;

	switch (err_type) {
	case AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE:
		sprintf(err_type_name, "correctable");
		break;
	case AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE:
		sprintf(err_type_name, "uncorrectable");
		break;
	default:
		sprintf(err_type_name, "unknown");
		break;
	}
}

bool amdgpu_ras_inst_get_memory_id_field(struct amdgpu_device *adev,
					 const struct amdgpu_ras_err_status_reg_entry *reg_entry,
					 uint32_t instance,
					 uint32_t *memory_id)
{
	uint32_t err_status_lo_data, err_status_lo_offset;

	if (!reg_entry)
		return false;

	err_status_lo_offset =
		AMDGPU_RAS_REG_ENTRY_OFFSET(reg_entry->hwip, instance,
					    reg_entry->seg_lo, reg_entry->reg_lo);
	err_status_lo_data = RREG32(err_status_lo_offset);

	if ((reg_entry->flags & AMDGPU_RAS_ERR_STATUS_VALID) &&
	    !REG_GET_FIELD(err_status_lo_data, ERR_STATUS_LO, ERR_STATUS_VALID_FLAG))
		return false;

	*memory_id = REG_GET_FIELD(err_status_lo_data, ERR_STATUS_LO, MEMORY_ID);

	return true;
}

bool amdgpu_ras_inst_get_err_cnt_field(struct amdgpu_device *adev,
				       const struct amdgpu_ras_err_status_reg_entry *reg_entry,
				       uint32_t instance,
				       unsigned long *err_cnt)
{
	uint32_t err_status_hi_data, err_status_hi_offset;

	if (!reg_entry)
		return false;

	err_status_hi_offset =
		AMDGPU_RAS_REG_ENTRY_OFFSET(reg_entry->hwip, instance,
					    reg_entry->seg_hi, reg_entry->reg_hi);
	err_status_hi_data = RREG32(err_status_hi_offset);

	if ((reg_entry->flags & AMDGPU_RAS_ERR_INFO_VALID) &&
	    !REG_GET_FIELD(err_status_hi_data, ERR_STATUS_HI, ERR_INFO_VALID_FLAG))
		/* keep the check here in case we need to refer to the result later */
		dev_dbg(adev->dev, "Invalid err_info field\n");

	/* read err count */
	*err_cnt = REG_GET_FIELD(err_status_hi_data, ERR_STATUS, ERR_CNT);

	return true;
}

void amdgpu_ras_inst_query_ras_error_count(struct amdgpu_device *adev,
					   const struct amdgpu_ras_err_status_reg_entry *reg_list,
					   uint32_t reg_list_size,
					   const struct amdgpu_ras_memory_id_entry *mem_list,
					   uint32_t mem_list_size,
					   uint32_t instance,
					   uint32_t err_type,
					   unsigned long *err_count)
{
	uint32_t memory_id;
	unsigned long err_cnt;
	char err_type_name[16];
	uint32_t i, j;

	for (i = 0; i < reg_list_size; i++) {
		/* query memory_id from err_status_lo */
		if (!amdgpu_ras_inst_get_memory_id_field(adev, &reg_list[i],
							 instance, &memory_id))
			continue;

		/* query err_cnt from err_status_hi */
		if (!amdgpu_ras_inst_get_err_cnt_field(adev, &reg_list[i],
						       instance, &err_cnt) ||
		    !err_cnt)
			continue;

		*err_count += err_cnt;

		/* log the errors */
		amdgpu_ras_get_error_type_name(err_type, err_type_name);
		if (!mem_list) {
			/* memory_list is not supported */
			dev_info(adev->dev,
				 "%ld %s hardware errors detected in %s, instance: %d, memory_id: %d\n",
				 err_cnt, err_type_name,
				 reg_list[i].block_name,
				 instance, memory_id);
		} else {
			for (j = 0; j < mem_list_size; j++) {
				if (memory_id == mem_list[j].memory_id) {
					dev_info(adev->dev,
						 "%ld %s hardware errors detected in %s, instance: %d, memory block: %s\n",
						 err_cnt, err_type_name,
						 reg_list[i].block_name,
						 instance, mem_list[j].name);
					break;
				}
			}
		}
	}
}

void amdgpu_ras_inst_reset_ras_error_count(struct amdgpu_device *adev,
					   const struct amdgpu_ras_err_status_reg_entry *reg_list,
					   uint32_t reg_list_size,
					   uint32_t instance)
{
	uint32_t err_status_lo_offset, err_status_hi_offset;
	uint32_t i;

	for (i = 0; i < reg_list_size; i++) {
		err_status_lo_offset =
			AMDGPU_RAS_REG_ENTRY_OFFSET(reg_list[i].hwip, instance,
						    reg_list[i].seg_lo, reg_list[i].reg_lo);
		err_status_hi_offset =
			AMDGPU_RAS_REG_ENTRY_OFFSET(reg_list[i].hwip, instance,
						    reg_list[i].seg_hi, reg_list[i].reg_hi);
		WREG32(err_status_lo_offset, 0);
		WREG32(err_status_hi_offset, 0);
	}
}

int amdgpu_ras_error_data_init(struct ras_err_data *err_data)
{
	memset(err_data, 0, sizeof(*err_data));

	INIT_LIST_HEAD(&err_data->err_node_list);

	return 0;
}

static void amdgpu_ras_error_node_release(struct ras_err_node *err_node)
{
	if (!err_node)
		return;

	list_del(&err_node->node);
	kvfree(err_node);
}

void amdgpu_ras_error_data_fini(struct ras_err_data *err_data)
{
	struct ras_err_node *err_node, *tmp;

	list_for_each_entry_safe(err_node, tmp, &err_data->err_node_list, node)
		amdgpu_ras_error_node_release(err_node);
}

static struct ras_err_node *amdgpu_ras_error_find_node_by_id(struct ras_err_data *err_data,
							     struct amdgpu_smuio_mcm_config_info *mcm_info)
{
	struct ras_err_node *err_node;
	struct amdgpu_smuio_mcm_config_info *ref_id;

	if (!err_data || !mcm_info)
		return NULL;

	for_each_ras_error(err_node, err_data) {
		ref_id = &err_node->err_info.mcm_info;

		if (mcm_info->socket_id == ref_id->socket_id &&
		    mcm_info->die_id == ref_id->die_id)
			return err_node;
	}

	return NULL;
}

static struct ras_err_node *amdgpu_ras_error_node_new(void)
{
	struct ras_err_node *err_node;

	err_node = kvzalloc(sizeof(*err_node), GFP_KERNEL);
	if (!err_node)
		return NULL;

	INIT_LIST_HEAD(&err_node->node);

	return err_node;
}

static int ras_err_info_cmp(void *priv, const struct list_head *a, const struct list_head *b)
{
	struct ras_err_node *nodea = container_of(a, struct ras_err_node, node);
	struct ras_err_node *nodeb = container_of(b, struct ras_err_node, node);
	struct amdgpu_smuio_mcm_config_info *infoa = &nodea->err_info.mcm_info;
	struct amdgpu_smuio_mcm_config_info *infob = &nodeb->err_info.mcm_info;

	if (unlikely(infoa->socket_id != infob->socket_id))
		return infoa->socket_id - infob->socket_id;
	else
		return infoa->die_id - infob->die_id;

	return 0;
}

static struct ras_err_info *amdgpu_ras_error_get_info(struct ras_err_data *err_data,
						      struct amdgpu_smuio_mcm_config_info *mcm_info)
{
	struct ras_err_node *err_node;

	err_node = amdgpu_ras_error_find_node_by_id(err_data, mcm_info);
	if (err_node)
		return &err_node->err_info;

	err_node = amdgpu_ras_error_node_new();
	if (!err_node)
		return NULL;

	memcpy(&err_node->err_info.mcm_info, mcm_info, sizeof(*mcm_info));

	err_data->err_list_count++;
	list_add_tail(&err_node->node, &err_data->err_node_list);
	list_sort(NULL, &err_data->err_node_list, ras_err_info_cmp);

	return &err_node->err_info;
}

int amdgpu_ras_error_statistic_ue_count(struct ras_err_data *err_data,
					struct amdgpu_smuio_mcm_config_info *mcm_info, u64 count)
{
	struct ras_err_info *err_info;

	if (!err_data || !mcm_info)
		return -EINVAL;

	if (!count)
		return 0;

	err_info = amdgpu_ras_error_get_info(err_data, mcm_info);
	if (!err_info)
		return -EINVAL;

	err_info->ue_count += count;
	err_data->ue_count += count;

	return 0;
}

int amdgpu_ras_error_statistic_ce_count(struct ras_err_data *err_data,
					struct amdgpu_smuio_mcm_config_info *mcm_info, u64 count)
{
	struct ras_err_info *err_info;

	if (!err_data || !mcm_info)
		return -EINVAL;

	if (!count)
		return 0;

	err_info = amdgpu_ras_error_get_info(err_data, mcm_info);
	if (!err_info)
		return -EINVAL;

	err_info->ce_count += count;
	err_data->ce_count += count;

	return 0;
}
