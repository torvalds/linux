// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI Platform Firmware Runtime Telemetry driver
 *
 * Copyright (C) 2021 Intel Corporation
 * Author: Chen Yu <yu.c.chen@intel.com>
 *
 * This driver allows user space to fetch telemetry data from the
 * firmware with the help of the Platform Firmware Runtime Telemetry
 * interface.
 */
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/uuid.h>

#include <uapi/linux/pfrut.h>

#define PFRT_LOG_EXEC_IDX	0
#define PFRT_LOG_HISTORY_IDX	1

#define PFRT_LOG_ERR		0
#define PFRT_LOG_WARN	1
#define PFRT_LOG_INFO	2
#define PFRT_LOG_VERB	4

#define PFRT_FUNC_SET_LEV		1
#define PFRT_FUNC_GET_LEV		2
#define PFRT_FUNC_GET_DATA		3

#define PFRT_REVID_1		1
#define PFRT_REVID_2		2
#define PFRT_DEFAULT_REV_ID	PFRT_REVID_1

enum log_index {
	LOG_STATUS_IDX = 0,
	LOG_EXT_STATUS_IDX = 1,
	LOG_MAX_SZ_IDX = 2,
	LOG_CHUNK1_LO_IDX = 3,
	LOG_CHUNK1_HI_IDX = 4,
	LOG_CHUNK1_SZ_IDX = 5,
	LOG_CHUNK2_LO_IDX = 6,
	LOG_CHUNK2_HI_IDX = 7,
	LOG_CHUNK2_SZ_IDX = 8,
	LOG_ROLLOVER_CNT_IDX = 9,
	LOG_RESET_CNT_IDX = 10,
	LOG_NR_IDX
};

struct pfrt_log_device {
	int index;
	struct pfrt_log_info info;
	struct device *parent_dev;
	struct miscdevice miscdev;
};

/* pfrt_guid is the parameter for _DSM method */
static const guid_t pfrt_log_guid =
	GUID_INIT(0x75191659, 0x8178, 0x4D9D, 0xB8, 0x8F, 0xAC, 0x5E,
		  0x5E, 0x93, 0xE8, 0xBF);

static DEFINE_IDA(pfrt_log_ida);

static inline struct pfrt_log_device *to_pfrt_log_dev(struct file *file)
{
	return container_of(file->private_data, struct pfrt_log_device, miscdev);
}

static int get_pfrt_log_data_info(struct pfrt_log_data_info *data_info,
				  struct pfrt_log_device *pfrt_log_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfrt_log_dev->parent_dev);
	union acpi_object *out_obj, in_obj, in_buf;
	int ret = -EBUSY;

	memset(data_info, 0, sizeof(*data_info));
	memset(&in_obj, 0, sizeof(in_obj));
	memset(&in_buf, 0, sizeof(in_buf));
	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = 1;
	in_obj.package.elements = &in_buf;
	in_buf.type = ACPI_TYPE_INTEGER;
	in_buf.integer.value = pfrt_log_dev->info.log_type;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfrt_log_guid,
					  pfrt_log_dev->info.log_revid, PFRT_FUNC_GET_DATA,
					  &in_obj, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return -EINVAL;

	if (out_obj->package.count < LOG_NR_IDX ||
	    out_obj->package.elements[LOG_STATUS_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_EXT_STATUS_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_MAX_SZ_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_CHUNK1_LO_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_CHUNK1_HI_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_CHUNK1_SZ_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_CHUNK2_LO_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_CHUNK2_HI_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_CHUNK2_SZ_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_ROLLOVER_CNT_IDX].type != ACPI_TYPE_INTEGER ||
	    out_obj->package.elements[LOG_RESET_CNT_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->status = out_obj->package.elements[LOG_STATUS_IDX].integer.value;
	data_info->ext_status =
		out_obj->package.elements[LOG_EXT_STATUS_IDX].integer.value;
	if (data_info->status != DSM_SUCCEED) {
		dev_dbg(pfrt_log_dev->parent_dev, "Error Status:%d\n", data_info->status);
		dev_dbg(pfrt_log_dev->parent_dev, "Error Extend Status:%d\n",
			data_info->ext_status);
		goto free_acpi_buffer;
	}

	data_info->max_data_size =
		out_obj->package.elements[LOG_MAX_SZ_IDX].integer.value;
	data_info->chunk1_addr_lo =
		out_obj->package.elements[LOG_CHUNK1_LO_IDX].integer.value;
	data_info->chunk1_addr_hi =
		out_obj->package.elements[LOG_CHUNK1_HI_IDX].integer.value;
	data_info->chunk1_size =
		out_obj->package.elements[LOG_CHUNK1_SZ_IDX].integer.value;
	data_info->chunk2_addr_lo =
		out_obj->package.elements[LOG_CHUNK2_LO_IDX].integer.value;
	data_info->chunk2_addr_hi =
		out_obj->package.elements[LOG_CHUNK2_HI_IDX].integer.value;
	data_info->chunk2_size =
		out_obj->package.elements[LOG_CHUNK2_SZ_IDX].integer.value;
	data_info->rollover_cnt =
		out_obj->package.elements[LOG_ROLLOVER_CNT_IDX].integer.value;
	data_info->reset_cnt =
		out_obj->package.elements[LOG_RESET_CNT_IDX].integer.value;

	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int set_pfrt_log_level(int level, struct pfrt_log_device *pfrt_log_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfrt_log_dev->parent_dev);
	union acpi_object *out_obj, *obj, in_obj, in_buf;
	enum pfru_dsm_status status, ext_status;
	int ret = 0;

	memset(&in_obj, 0, sizeof(in_obj));
	memset(&in_buf, 0, sizeof(in_buf));
	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = 1;
	in_obj.package.elements = &in_buf;
	in_buf.type = ACPI_TYPE_INTEGER;
	in_buf.integer.value = level;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfrt_log_guid,
					  pfrt_log_dev->info.log_revid, PFRT_FUNC_SET_LEV,
					  &in_obj, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return -EINVAL;

	obj = &out_obj->package.elements[0];
	status = obj->integer.value;
	if (status != DSM_SUCCEED) {
		obj = &out_obj->package.elements[1];
		ext_status = obj->integer.value;
		dev_dbg(pfrt_log_dev->parent_dev, "Error Status:%d\n", status);
		dev_dbg(pfrt_log_dev->parent_dev, "Error Extend Status:%d\n", ext_status);
		ret = -EBUSY;
	}

	ACPI_FREE(out_obj);

	return ret;
}

static int get_pfrt_log_level(struct pfrt_log_device *pfrt_log_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfrt_log_dev->parent_dev);
	union acpi_object *out_obj, *obj;
	enum pfru_dsm_status status, ext_status;
	int ret = -EBUSY;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfrt_log_guid,
					  pfrt_log_dev->info.log_revid, PFRT_FUNC_GET_LEV,
					  NULL, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return -EINVAL;

	obj = &out_obj->package.elements[0];
	if (obj->type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	status = obj->integer.value;
	if (status != DSM_SUCCEED) {
		obj = &out_obj->package.elements[1];
		ext_status = obj->integer.value;
		dev_dbg(pfrt_log_dev->parent_dev, "Error Status:%d\n", status);
		dev_dbg(pfrt_log_dev->parent_dev, "Error Extend Status:%d\n", ext_status);
		goto free_acpi_buffer;
	}

	obj = &out_obj->package.elements[2];
	if (obj->type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	ret = obj->integer.value;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int valid_log_level(u32 level)
{
	return level == PFRT_LOG_ERR || level == PFRT_LOG_WARN ||
	       level == PFRT_LOG_INFO || level == PFRT_LOG_VERB;
}

static int valid_log_type(u32 type)
{
	return type == PFRT_LOG_EXEC_IDX || type == PFRT_LOG_HISTORY_IDX;
}

static inline int valid_log_revid(u32 id)
{
	return id == PFRT_REVID_1 || id == PFRT_REVID_2;
}

static long pfrt_log_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pfrt_log_device *pfrt_log_dev = to_pfrt_log_dev(file);
	struct pfrt_log_data_info data_info;
	struct pfrt_log_info info;
	void __user *p;
	int ret = 0;

	p = (void __user *)arg;

	switch (cmd) {
	case PFRT_LOG_IOC_SET_INFO:
		if (copy_from_user(&info, p, sizeof(info)))
			return -EFAULT;

		if (valid_log_revid(info.log_revid))
			pfrt_log_dev->info.log_revid = info.log_revid;

		if (valid_log_level(info.log_level)) {
			ret = set_pfrt_log_level(info.log_level, pfrt_log_dev);
			if (ret < 0)
				return ret;

			pfrt_log_dev->info.log_level = info.log_level;
		}

		if (valid_log_type(info.log_type))
			pfrt_log_dev->info.log_type = info.log_type;

		return 0;

	case PFRT_LOG_IOC_GET_INFO:
		info.log_level = get_pfrt_log_level(pfrt_log_dev);
		if (ret < 0)
			return ret;

		info.log_type = pfrt_log_dev->info.log_type;
		info.log_revid = pfrt_log_dev->info.log_revid;
		if (copy_to_user(p, &info, sizeof(info)))
			return -EFAULT;

		return 0;

	case PFRT_LOG_IOC_GET_DATA_INFO:
		ret = get_pfrt_log_data_info(&data_info, pfrt_log_dev);
		if (ret)
			return ret;

		if (copy_to_user(p, &data_info, sizeof(struct pfrt_log_data_info)))
			return -EFAULT;

		return 0;

	default:
		return -ENOTTY;
	}
}

static int
pfrt_log_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pfrt_log_device *pfrt_log_dev;
	struct pfrt_log_data_info info;
	unsigned long psize, vsize;
	phys_addr_t base_addr;
	int ret;

	if (vma->vm_flags & VM_WRITE)
		return -EROFS;

	/* changing from read to write with mprotect is not allowed */
	vm_flags_clear(vma, VM_MAYWRITE);

	pfrt_log_dev = to_pfrt_log_dev(file);

	ret = get_pfrt_log_data_info(&info, pfrt_log_dev);
	if (ret)
		return ret;

	base_addr = (phys_addr_t)((info.chunk2_addr_hi << 32) | info.chunk2_addr_lo);
	/* pfrt update has not been launched yet */
	if (!base_addr)
		return -ENODEV;

	psize = info.max_data_size;
	/* base address and total buffer size must be page aligned */
	if (!PAGE_ALIGNED(base_addr) || !PAGE_ALIGNED(psize))
		return -ENODEV;

	vsize = vma->vm_end - vma->vm_start;
	if (vsize > psize)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (io_remap_pfn_range(vma, vma->vm_start, PFN_DOWN(base_addr),
			       vsize, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static const struct file_operations acpi_pfrt_log_fops = {
	.owner		= THIS_MODULE,
	.mmap		= pfrt_log_mmap,
	.unlocked_ioctl = pfrt_log_ioctl,
	.llseek		= noop_llseek,
};

static int acpi_pfrt_log_remove(struct platform_device *pdev)
{
	struct pfrt_log_device *pfrt_log_dev = platform_get_drvdata(pdev);

	misc_deregister(&pfrt_log_dev->miscdev);

	return 0;
}

static void pfrt_log_put_idx(void *data)
{
	struct pfrt_log_device *pfrt_log_dev = data;

	ida_free(&pfrt_log_ida, pfrt_log_dev->index);
}

static int acpi_pfrt_log_probe(struct platform_device *pdev)
{
	acpi_handle handle = ACPI_HANDLE(&pdev->dev);
	struct pfrt_log_device *pfrt_log_dev;
	int ret;

	if (!acpi_has_method(handle, "_DSM")) {
		dev_dbg(&pdev->dev, "Missing _DSM\n");
		return -ENODEV;
	}

	pfrt_log_dev = devm_kzalloc(&pdev->dev, sizeof(*pfrt_log_dev), GFP_KERNEL);
	if (!pfrt_log_dev)
		return -ENOMEM;

	ret = ida_alloc(&pfrt_log_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;

	pfrt_log_dev->index = ret;
	ret = devm_add_action_or_reset(&pdev->dev, pfrt_log_put_idx, pfrt_log_dev);
	if (ret)
		return ret;

	pfrt_log_dev->info.log_revid = PFRT_DEFAULT_REV_ID;
	pfrt_log_dev->parent_dev = &pdev->dev;

	pfrt_log_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	pfrt_log_dev->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
						    "pfrt%d",
						    pfrt_log_dev->index);
	if (!pfrt_log_dev->miscdev.name)
		return -ENOMEM;

	pfrt_log_dev->miscdev.nodename = devm_kasprintf(&pdev->dev, GFP_KERNEL,
							"acpi_pfr_telemetry%d",
							pfrt_log_dev->index);
	if (!pfrt_log_dev->miscdev.nodename)
		return -ENOMEM;

	pfrt_log_dev->miscdev.fops = &acpi_pfrt_log_fops;
	pfrt_log_dev->miscdev.parent = &pdev->dev;

	ret = misc_register(&pfrt_log_dev->miscdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pfrt_log_dev);

	return 0;
}

static const struct acpi_device_id acpi_pfrt_log_ids[] = {
	{"INTC1081"},
	{}
};
MODULE_DEVICE_TABLE(acpi, acpi_pfrt_log_ids);

static struct platform_driver acpi_pfrt_log_driver = {
	.driver = {
		.name = "pfr_telemetry",
		.acpi_match_table = acpi_pfrt_log_ids,
	},
	.probe = acpi_pfrt_log_probe,
	.remove = acpi_pfrt_log_remove,
};
module_platform_driver(acpi_pfrt_log_driver);

MODULE_DESCRIPTION("Platform Firmware Runtime Update Telemetry driver");
MODULE_LICENSE("GPL v2");
