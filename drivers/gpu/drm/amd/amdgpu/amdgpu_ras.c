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

#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_xgmi.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"

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

#define AMDGPU_RAS_FLAG_INIT_BY_VBIOS		1
#define AMDGPU_RAS_FLAG_INIT_NEED_RESET		2
#define RAS_DEFAULT_FLAGS (AMDGPU_RAS_FLAG_INIT_BY_VBIOS)

/* inject address is 52 bits */
#define	RAS_UMC_INJECT_ADDR_LIMIT	(0x1ULL << 52)

enum amdgpu_ras_retire_page_reservation {
	AMDGPU_RAS_RETIRE_PAGE_RESERVED,
	AMDGPU_RAS_RETIRE_PAGE_PENDING,
	AMDGPU_RAS_RETIRE_PAGE_FAULT,
};

atomic_t amdgpu_ras_in_intr = ATOMIC_INIT(0);

static bool amdgpu_ras_check_bad_page(struct amdgpu_device *adev,
				uint64_t addr);

static ssize_t amdgpu_ras_debugfs_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct ras_manager *obj = (struct ras_manager *)file_inode(f)->i_private;
	struct ras_query_if info = {
		.head = obj->head,
	};
	ssize_t s;
	char val[128];

	if (amdgpu_ras_error_query(obj->adev, &info))
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
	else if (str[0] && str[1] && str[2] && str[3])
		/* ascii string, but commands are not matched. */
		return -EINVAL;

	if (op != -1) {
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
			if (sscanf(str, "%*s %*s %*s %u %llu %llu",
						&sub_block, &address, &value) != 3)
				if (sscanf(str, "%*s %*s %*s 0x%x 0x%llx 0x%llx",
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
 * It accepts struct ras_debug_if who has two members.
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
 * Programs
 *
 * Copy the struct ras_debug_if in your codes and initialize it.
 * Write the struct to the control node.
 *
 * Shells
 *
 * .. code-block:: bash
 *
 *	echo op block [error [sub_block address value]] > .../ras/ras_ctrl
 *
 * Parameters:
 *
 * op: disable, enable, inject
 *	disable: only block is needed
 *	enable: block and error are needed
 *	inject: error, address, value are needed
 * block: umc, sdma, gfx, .........
 *	see ras_block_string[] for details
 * error: ue, ce
 *	ue: multi_uncorrectable
 *	ce: single_correctable
 * sub_block:
 *	sub block index, pass 0 if there is no sub block
 *
 * here are some examples for bash commands:
 *
 * .. code-block:: bash
 *
 *	echo inject umc ue 0x0 0x0 0x0 > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *	echo inject umc ce 0 0 0 > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *	echo disable umc > /sys/kernel/debug/dri/0/ras/ras_ctrl
 *
 * How to check the result?
 *
 * For disable/enable, please check ras features at
 * /sys/class/drm/card[0/1/2...]/device/ras/features
 *
 * For inject, please check corresponding err count at
 * /sys/class/drm/card[0/1/2...]/device/ras/[gfx/sdma/...]_err_count
 *
 * .. note::
 *	Operations are only allowed on blocks which are supported.
 *	Please check ras mask at /sys/module/amdgpu/parameters/ras_mask
 *	to see which blocks support RAS on a particular asic.
 *
 */
static ssize_t amdgpu_ras_debugfs_ctrl_write(struct file *f, const char __user *buf,
		size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct ras_debug_if data;
	int ret = 0;

	if (amdgpu_ras_intr_triggered()) {
		DRM_WARN("RAS WARN: error injection currently inaccessible\n");
		return size;
	}

	ret = amdgpu_ras_debugfs_ctrl_parse_data(f, buf, size, pos, &data);
	if (ret)
		return -EINVAL;

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
			ret = -EINVAL;
			break;
		}

		/* umc ce/ue error injection for a bad page is not allowed */
		if ((data.head.block == AMDGPU_RAS_BLOCK__UMC) &&
		    amdgpu_ras_check_bad_page(adev, data.inject.address)) {
			DRM_WARN("RAS WARN: 0x%llx has been marked as bad before error injection!\n",
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
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	int ret;

	ret = amdgpu_ras_eeprom_reset_table(&adev->psp.ras.ras->eeprom_control);

	return ret == 1 ? size : -EIO;
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

	if (amdgpu_ras_intr_triggered())
		return snprintf(buf, PAGE_SIZE,
				"Query currently inaccessible\n");

	if (amdgpu_ras_error_query(obj->adev, &info))
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%s: %lu\n%s: %lu\n",
			"ue", info.ue_count,
			"ce", info.ce_count);
}

/* obj begin */

#define get_obj(obj) do { (obj)->use++; } while (0)
#define alive_obj(obj) ((obj)->use)

static inline void put_obj(struct ras_manager *obj)
{
	if (obj && --obj->use == 0)
		list_del(&obj->node);
	if (obj && obj->use < 0) {
		 DRM_ERROR("RAS ERROR: Unbalance obj(%s) use\n", obj->head.name);
	}
}

/* make one obj and return it. */
static struct ras_manager *amdgpu_ras_create_obj(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;

	if (!con)
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

	if (!con)
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
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	return con->hw_supported & BIT(head->block);
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
	union ta_ras_cmd_input info;
	int ret;

	if (!con)
		return -EINVAL;

	if (!enable) {
		info.disable_features = (struct ta_ras_disable_features_input) {
			.block_id =  amdgpu_ras_block_to_ta(head->block),
			.error_type = amdgpu_ras_error_to_ta(head->type),
		};
	} else {
		info.enable_features = (struct ta_ras_enable_features_input) {
			.block_id =  amdgpu_ras_block_to_ta(head->block),
			.error_type = amdgpu_ras_error_to_ta(head->type),
		};
	}

	/* Do not enable if it is not allowed. */
	WARN_ON(enable && !amdgpu_ras_is_feature_allowed(adev, head));
	/* Are we alerady in that state we are going to set? */
	if (!(!!enable ^ !!amdgpu_ras_is_feature_enabled(adev, head)))
		return 0;

	if (!amdgpu_ras_intr_triggered()) {
		ret = psp_ras_enable_features(&adev->psp, &info, enable);
		if (ret) {
			DRM_ERROR("RAS ERROR: %s %s feature failed ret %d\n",
					enable ? "enable":"disable",
					ras_block_str(head->block),
					ret);
			if (ret == TA_RAS_STATUS__RESET_NEEDED)
				return -EAGAIN;
			return -EINVAL;
		}
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
					DRM_INFO("RAS INFO: %s setup object\n",
						ras_block_str(head->block));
			}
		} else {
			/* setup the object then issue a ras TA disable cmd.*/
			ret = __amdgpu_ras_feature_enable(adev, head, 1);
			if (ret)
				return ret;

			ret = amdgpu_ras_feature_enable(adev, head, 0);
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
int amdgpu_ras_error_query(struct amdgpu_device *adev,
		struct ras_query_if *info)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &info->head);
	struct ras_err_data err_data = {0, 0, 0, NULL};
	int i;

	if (!obj)
		return -EINVAL;

	switch (info->head.block) {
	case AMDGPU_RAS_BLOCK__UMC:
		if (adev->umc.funcs->query_ras_error_count)
			adev->umc.funcs->query_ras_error_count(adev, &err_data);
		/* umc query_ras_error_address is also responsible for clearing
		 * error status
		 */
		if (adev->umc.funcs->query_ras_error_address)
			adev->umc.funcs->query_ras_error_address(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__SDMA:
		if (adev->sdma.funcs->query_ras_error_count) {
			for (i = 0; i < adev->sdma.num_instances; i++)
				adev->sdma.funcs->query_ras_error_count(adev, i,
									&err_data);
		}
		break;
	case AMDGPU_RAS_BLOCK__GFX:
		if (adev->gfx.funcs->query_ras_error_count)
			adev->gfx.funcs->query_ras_error_count(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__MMHUB:
		if (adev->mmhub.funcs->query_ras_error_count)
			adev->mmhub.funcs->query_ras_error_count(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		if (adev->nbio.funcs->query_ras_error_count)
			adev->nbio.funcs->query_ras_error_count(adev, &err_data);
		break;
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
		amdgpu_xgmi_query_ras_error_count(adev, &err_data);
		break;
	default:
		break;
	}

	obj->err_data.ue_count += err_data.ue_count;
	obj->err_data.ce_count += err_data.ce_count;

	info->ue_count = obj->err_data.ue_count;
	info->ce_count = obj->err_data.ce_count;

	if (err_data.ce_count) {
		dev_info(adev->dev, "%ld correctable errors detected in %s block\n",
			 obj->err_data.ce_count, ras_block_str(info->head.block));
	}
	if (err_data.ue_count) {
		dev_info(adev->dev, "%ld uncorrectable errors detected in %s block\n",
			 obj->err_data.ue_count, ras_block_str(info->head.block));
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
		if (adev->gfx.funcs->ras_error_inject)
			ret = adev->gfx.funcs->ras_error_inject(adev, info);
		else
			ret = -EINVAL;
		break;
	case AMDGPU_RAS_BLOCK__UMC:
	case AMDGPU_RAS_BLOCK__MMHUB:
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		ret = psp_ras_trigger_error(&adev->psp, &block_info);
		break;
	default:
		DRM_INFO("%s error injection is not supported yet\n",
			 ras_block_str(info->head.block));
		ret = -EINVAL;
	}

	if (ret)
		DRM_ERROR("RAS ERROR: inject %s error failed ret %d\n",
				ras_block_str(info->head.block),
				ret);

	return ret;
}

int amdgpu_ras_error_cure(struct amdgpu_device *adev,
		struct ras_cure_if *info)
{
	/* psp fw has no cure interface for now. */
	return 0;
}

/* get the total error counts on all IPs */
unsigned long amdgpu_ras_query_error_count(struct amdgpu_device *adev,
		bool is_ce)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;
	struct ras_err_data data = {0, 0};

	if (!con)
		return 0;

	list_for_each_entry(obj, &con->head, node) {
		struct ras_query_if info = {
			.head = obj->head,
		};

		if (amdgpu_ras_error_query(adev, &info))
			return 0;

		data.ce_count += info.ce_count;
		data.ue_count += info.ue_count;
	}

	return is_ce ? data.ce_count : data.ue_count;
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
	};
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

static int amdgpu_ras_sysfs_create_feature_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct attribute *attrs[] = {
		&con->features_attr.attr,
		NULL
	};
	struct bin_attribute *bin_attrs[] = {
		&con->badpages_attr,
		NULL
	};
	struct attribute_group group = {
		.name = "ras",
		.attrs = attrs,
		.bin_attrs = bin_attrs,
	};

	con->features_attr = (struct device_attribute) {
		.attr = {
			.name = "features",
			.mode = S_IRUGO,
		},
			.show = amdgpu_ras_sysfs_features_read,
	};

	con->badpages_attr = (struct bin_attribute) {
		.attr = {
			.name = "gpu_vram_bad_pages",
			.mode = S_IRUGO,
		},
		.size = 0,
		.private = NULL,
		.read = amdgpu_ras_sysfs_badpages_read,
	};

	sysfs_attr_init(attrs[0]);
	sysfs_bin_attr_init(bin_attrs[0]);

	return sysfs_create_group(&adev->dev->kobj, &group);
}

static int amdgpu_ras_sysfs_remove_feature_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct attribute *attrs[] = {
		&con->features_attr.attr,
		NULL
	};
	struct bin_attribute *bin_attrs[] = {
		&con->badpages_attr,
		NULL
	};
	struct attribute_group group = {
		.name = "ras",
		.attrs = attrs,
		.bin_attrs = bin_attrs,
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
				"ras")) {
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
				"ras");
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
static void amdgpu_ras_debugfs_create_ctrl_node(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct drm_minor *minor = adev->ddev->primary;

	con->dir = debugfs_create_dir("ras", minor->debugfs_root);
	debugfs_create_file("ras_ctrl", S_IWUGO | S_IRUGO, con->dir,
				adev, &amdgpu_ras_debugfs_ctrl_ops);
	debugfs_create_file("ras_eeprom_reset", S_IWUGO | S_IRUGO, con->dir,
				adev, &amdgpu_ras_debugfs_eeprom_ops);

	/*
	 * After one uncorrectable error happens, usually GPU recovery will
	 * be scheduled. But due to the known problem in GPU recovery failing
	 * to bring GPU back, below interface provides one direct way to
	 * user to reboot system automatically in such case within
	 * ERREVENT_ATHUB_INTERRUPT generated. Normal GPU recovery routine
	 * will never be called.
	 */
	debugfs_create_bool("auto_reboot", S_IWUGO | S_IRUGO, con->dir,
				&con->reboot);
}

void amdgpu_ras_debugfs_create(struct amdgpu_device *adev,
		struct ras_fs_if *head)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, &head->head);

	if (!obj || obj->ent)
		return;

	get_obj(obj);

	memcpy(obj->fs_data.debugfs_name,
			head->debugfs_name,
			sizeof(obj->fs_data.debugfs_name));

	obj->ent = debugfs_create_file(obj->fs_data.debugfs_name,
				       S_IWUGO | S_IRUGO, con->dir, obj,
				       &amdgpu_ras_debugfs_ops);
}

void amdgpu_ras_debugfs_create_all(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj;
	struct ras_fs_if fs_info;

	/*
	 * it won't be called in resume path, no need to check
	 * suspend and gpu reset status
	 */
	if (!con)
		return;

	amdgpu_ras_debugfs_create_ctrl_node(adev);

	list_for_each_entry(obj, &con->head, node) {
		if (amdgpu_ras_is_supported(adev, obj->head.block) &&
			(obj->attr_inuse == 1)) {
			sprintf(fs_info.debugfs_name, "%s_err_inject",
					ras_block_str(obj->head.block));
			fs_info.head = obj->head;
			amdgpu_ras_debugfs_create(adev, &fs_info);
		}
	}
}

void amdgpu_ras_debugfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head)
{
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, head);

	if (!obj || !obj->ent)
		return;

	debugfs_remove(obj->ent);
	obj->ent = NULL;
	put_obj(obj);
}

static void amdgpu_ras_debugfs_remove_all(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_manager *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &con->head, node) {
		amdgpu_ras_debugfs_remove(adev, &obj->head);
	}

	debugfs_remove_recursive(con->dir);
	con->dir = NULL;
}
/* debugfs end */

/* ras fs */

static int amdgpu_ras_fs_init(struct amdgpu_device *adev)
{
	amdgpu_ras_sysfs_create_feature_node(adev);

	return 0;
}

static int amdgpu_ras_fs_fini(struct amdgpu_device *adev)
{
	amdgpu_ras_debugfs_remove_all(adev);
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

	if (!con)
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

		amdgpu_ras_error_query(adev, &info);
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
	int ret = 0;

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

		if (data->last_reserved <= i)
			(*bps)[i].flags = AMDGPU_RAS_RETIRE_PAGE_PENDING;
		else if (data->bps_bo[i] == NULL)
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

	/*
	 * Query and print non zero error counter per IP block for
	 * awareness before recovering GPU.
	 */
	amdgpu_ras_log_on_err_counter(ras->adev);

	if (amdgpu_device_should_recover_gpu(ras->adev))
		amdgpu_device_gpu_recover(ras->adev, 0);
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
	struct amdgpu_bo **bps_bo =
			kmalloc(align_space * sizeof(*data->bps_bo), GFP_KERNEL);

	if (!bps || !bps_bo) {
		kfree(bps);
		kfree(bps_bo);
		return -ENOMEM;
	}

	if (data->bps) {
		memcpy(bps, data->bps,
				data->count * sizeof(*data->bps));
		kfree(data->bps);
	}
	if (data->bps_bo) {
		memcpy(bps_bo, data->bps_bo,
				data->count * sizeof(*data->bps_bo));
		kfree(data->bps_bo);
	}

	data->bps = bps;
	data->bps_bo = bps_bo;
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

	if (!con || !con->eh_data || !bps || pages <= 0)
		return 0;

	mutex_lock(&con->recovery_lock);
	data = con->eh_data;
	if (!data)
		goto out;

	if (data->space_left <= pages)
		if (amdgpu_ras_realloc_eh_data_space(adev, data, pages)) {
			ret = -ENOMEM;
			goto out;
		}

	memcpy(&data->bps[data->count], bps, pages * sizeof(*data->bps));
	data->count += pages;
	data->space_left -= pages;

out:
	mutex_unlock(&con->recovery_lock);

	return ret;
}

/*
 * write error record array to eeprom, the function should be
 * protected by recovery_lock
 */
static int amdgpu_ras_save_bad_pages(struct amdgpu_device *adev)
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
	if (save_count > 0)
		if (amdgpu_ras_eeprom_process_recods(control,
							&data->bps[control->num_recs],
							true,
							save_count)) {
			DRM_ERROR("Failed to save EEPROM table data!");
			return -EIO;
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
	if (!control->num_recs)
		return ret;

	bps = kcalloc(control->num_recs, sizeof(*bps), GFP_KERNEL);
	if (!bps)
		return -ENOMEM;

	if (amdgpu_ras_eeprom_process_recods(control, bps, false,
		control->num_recs)) {
		DRM_ERROR("Failed to load EEPROM table records!");
		ret = -EIO;
		goto out;
	}

	ret = amdgpu_ras_add_bad_pages(adev, bps, control->num_recs);

out:
	kfree(bps);
	return ret;
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
	struct ras_err_handler_data *data;
	int i;
	bool ret = false;

	if (!con || !con->eh_data)
		return ret;

	mutex_lock(&con->recovery_lock);
	data = con->eh_data;
	if (!data)
		goto out;

	addr >>= AMDGPU_GPU_PAGE_SHIFT;
	for (i = 0; i < data->count; i++)
		if (addr == data->bps[i].retired_page) {
			ret = true;
			goto out;
		}

out:
	mutex_unlock(&con->recovery_lock);
	return ret;
}

/* called in gpu recovery/init */
int amdgpu_ras_reserve_bad_pages(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data;
	uint64_t bp;
	struct amdgpu_bo *bo = NULL;
	int i, ret = 0;

	if (!con || !con->eh_data)
		return 0;

	mutex_lock(&con->recovery_lock);
	data = con->eh_data;
	if (!data)
		goto out;
	/* reserve vram at driver post stage. */
	for (i = data->last_reserved; i < data->count; i++) {
		bp = data->bps[i].retired_page;

		/* There are two cases of reserve error should be ignored:
		 * 1) a ras bad page has been allocated (used by someone);
		 * 2) a ras bad page has been reserved (duplicate error injection
		 *    for one page);
		 */
		if (amdgpu_bo_create_kernel_at(adev, bp << AMDGPU_GPU_PAGE_SHIFT,
					       AMDGPU_GPU_PAGE_SIZE,
					       AMDGPU_GEM_DOMAIN_VRAM,
					       &bo, NULL))
			DRM_WARN("RAS WARN: reserve vram for retired page %llx fail\n", bp);

		data->bps_bo[i] = bo;
		data->last_reserved = i + 1;
		bo = NULL;
	}

	/* continue to save bad pages to eeprom even reesrve_vram fails */
	ret = amdgpu_ras_save_bad_pages(adev);
out:
	mutex_unlock(&con->recovery_lock);
	return ret;
}

/* called when driver unload */
static int amdgpu_ras_release_bad_pages(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data *data;
	struct amdgpu_bo *bo;
	int i;

	if (!con || !con->eh_data)
		return 0;

	mutex_lock(&con->recovery_lock);
	data = con->eh_data;
	if (!data)
		goto out;

	for (i = data->last_reserved - 1; i >= 0; i--) {
		bo = data->bps_bo[i];

		amdgpu_bo_free_kernel(&bo, NULL, NULL);

		data->bps_bo[i] = bo;
		data->last_reserved = i;
	}
out:
	mutex_unlock(&con->recovery_lock);
	return 0;
}

int amdgpu_ras_recovery_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_err_handler_data **data;
	int ret;

	if (con)
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

	ret = amdgpu_ras_eeprom_init(&con->eeprom_control);
	if (ret)
		goto free;

	if (con->eeprom_control.num_recs) {
		ret = amdgpu_ras_load_bad_pages(adev);
		if (ret)
			goto free;
		ret = amdgpu_ras_reserve_bad_pages(adev);
		if (ret)
			goto release;
	}

	return 0;

release:
	amdgpu_ras_release_bad_pages(adev);
free:
	kfree((*data)->bps);
	kfree((*data)->bps_bo);
	kfree(*data);
	con->eh_data = NULL;
out:
	DRM_WARN("Failed to initialize ras recovery!\n");

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
	amdgpu_ras_release_bad_pages(adev);

	mutex_lock(&con->recovery_lock);
	con->eh_data = NULL;
	kfree(data->bps);
	kfree(data->bps_bo);
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

/*
 * check hardware's ras ability which will be saved in hw_supported.
 * if hardware does not support ras, we can skip some ras initializtion and
 * forbid some ras operations from IP.
 * if software itself, say boot parameter, limit the ras ability. We still
 * need allow IP do some limited operations, like disable. In such case,
 * we have to initialize ras as normal. but need check if operation is
 * allowed or not in each function.
 */
static void amdgpu_ras_check_supported(struct amdgpu_device *adev,
		uint32_t *hw_supported, uint32_t *supported)
{
	*hw_supported = 0;
	*supported = 0;

	if (amdgpu_sriov_vf(adev) || !adev->is_atom_fw ||
	    (adev->asic_type != CHIP_VEGA20 &&
	     adev->asic_type != CHIP_ARCTURUS))
		return;

	if (amdgpu_atomfirmware_mem_ecc_supported(adev)) {
		DRM_INFO("HBM ECC is active.\n");
		*hw_supported |= (1 << AMDGPU_RAS_BLOCK__UMC |
				1 << AMDGPU_RAS_BLOCK__DF);
	} else
		DRM_INFO("HBM ECC is not presented.\n");

	if (amdgpu_atomfirmware_sram_ecc_supported(adev)) {
		DRM_INFO("SRAM ECC is active.\n");
		*hw_supported |= ~(1 << AMDGPU_RAS_BLOCK__UMC |
				1 << AMDGPU_RAS_BLOCK__DF);
	} else
		DRM_INFO("SRAM ECC is not presented.\n");

	/* hw_supported needs to be aligned with RAS block mask. */
	*hw_supported &= AMDGPU_RAS_BLOCK_MASK;

	*supported = amdgpu_ras_enable == 0 ?
			0 : *hw_supported & amdgpu_ras_mask;
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

	con->objs = (struct ras_manager *)(con + 1);

	amdgpu_ras_set_context(adev, con);

	amdgpu_ras_check_supported(adev, &con->hw_supported,
			&con->supported);
	if (!con->hw_supported) {
		amdgpu_ras_set_context(adev, NULL);
		kfree(con);
		return 0;
	}

	con->features = 0;
	INIT_LIST_HEAD(&con->head);
	/* Might need get this flag from vbios. */
	con->flags = RAS_DEFAULT_FLAGS;

	if (adev->nbio.funcs->init_ras_controller_interrupt) {
		r = adev->nbio.funcs->init_ras_controller_interrupt(adev);
		if (r)
			return r;
	}

	if (adev->nbio.funcs->init_ras_err_event_athub_interrupt) {
		r = adev->nbio.funcs->init_ras_err_event_athub_interrupt(adev);
		if (r)
			return r;
	}

	amdgpu_ras_mask &= AMDGPU_RAS_BLOCK_MASK;

	if (amdgpu_ras_fs_init(adev))
		goto fs_out;

	DRM_INFO("RAS INFO: ras initialized successfully, "
			"hardware ability[%x] ras_mask[%x]\n",
			con->hw_supported, con->supported);
	return 0;
fs_out:
	amdgpu_ras_set_context(adev, NULL);
	kfree(con);

	return -EINVAL;
}

/* helper function to handle common stuff in ip late init phase */
int amdgpu_ras_late_init(struct amdgpu_device *adev,
			 struct ras_common_if *ras_block,
			 struct ras_fs_if *fs_info,
			 struct ras_ih_if *ih_info)
{
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
		} else if (adev->in_suspend || adev->in_gpu_reset) {
			/* in resume phase, if fail to enable ras,
			 * clean up all ras fs nodes, and disable ras */
			goto cleanup;
		} else
			return r;
	}

	/* in resume phase, no need to create ras fs node */
	if (adev->in_suspend || adev->in_gpu_reset)
		return 0;

	if (ih_info->cb) {
		r = amdgpu_ras_interrupt_add_handler(adev, ih_info);
		if (r)
			goto interrupt;
	}

	r = amdgpu_ras_sysfs_create(adev, fs_info);
	if (r)
		goto sysfs;

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

	if (!con)
		return;

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

	if (!con)
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

	if (!con)
		return 0;

	/* Need disable ras on all IPs here before ip [hw/sw]fini */
	amdgpu_ras_disable_all_features(adev, 0);
	amdgpu_ras_recovery_fini(adev);
	return 0;
}

int amdgpu_ras_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!con)
		return 0;

	amdgpu_ras_fs_fini(adev);
	amdgpu_ras_interrupt_remove_all(adev);

	WARN(con->features, "Feature mask is not cleared");

	if (con->features)
		amdgpu_ras_disable_all_features(adev, 1);

	amdgpu_ras_set_context(adev, NULL);
	kfree(con);

	return 0;
}

void amdgpu_ras_global_ras_isr(struct amdgpu_device *adev)
{
	uint32_t hw_supported, supported;

	amdgpu_ras_check_supported(adev, &hw_supported, &supported);
	if (!hw_supported)
		return;

	if (atomic_cmpxchg(&amdgpu_ras_in_intr, 0, 1) == 0) {
		DRM_WARN("RAS event of type ERREVENT_ATHUB_INTERRUPT detected!\n");

		amdgpu_ras_reset_gpu(adev);
	}
}
