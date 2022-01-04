// SPDX-License-Identifier: GPL-2.0-only

/*
 * FPDT support for exporting boot and suspend/resume performance data
 *
 * Copyright (C) 2021 Intel Corporation. All rights reserved.
 */

#define pr_fmt(fmt) "ACPI FPDT: " fmt

#include <linux/acpi.h>

/*
 * FPDT contains ACPI table header and a number of fpdt_subtable_entries.
 * Each fpdt_subtable_entry points to a subtable: FBPT or S3PT.
 * Each FPDT subtable (FBPT/S3PT) is composed of a fpdt_subtable_header
 * and a number of fpdt performance records.
 * Each FPDT performance record is composed of a fpdt_record_header and
 * performance data fields, for boot or suspend or resume phase.
 */
enum fpdt_subtable_type {
	SUBTABLE_FBPT,
	SUBTABLE_S3PT,
};

struct fpdt_subtable_entry {
	u16 type;		/* refer to enum fpdt_subtable_type */
	u8 length;
	u8 revision;
	u32 reserved;
	u64 address;		/* physical address of the S3PT/FBPT table */
};

struct fpdt_subtable_header {
	u32 signature;
	u32 length;
};

enum fpdt_record_type {
	RECORD_S3_RESUME,
	RECORD_S3_SUSPEND,
	RECORD_BOOT,
};

struct fpdt_record_header {
	u16 type;		/* refer to enum fpdt_record_type */
	u8 length;
	u8 revision;
};

struct resume_performance_record {
	struct fpdt_record_header header;
	u32 resume_count;
	u64 resume_prev;
	u64 resume_avg;
} __attribute__((packed));

struct boot_performance_record {
	struct fpdt_record_header header;
	u32 reserved;
	u64 firmware_start;
	u64 bootloader_load;
	u64 bootloader_launch;
	u64 exitbootservice_start;
	u64 exitbootservice_end;
} __attribute__((packed));

struct suspend_performance_record {
	struct fpdt_record_header header;
	u64 suspend_start;
	u64 suspend_end;
} __attribute__((packed));


static struct resume_performance_record *record_resume;
static struct suspend_performance_record *record_suspend;
static struct boot_performance_record *record_boot;

#define FPDT_ATTR(phase, name)	\
static ssize_t name##_show(struct kobject *kobj,	\
		 struct kobj_attribute *attr, char *buf)	\
{	\
	return sprintf(buf, "%llu\n", record_##phase->name);	\
}	\
static struct kobj_attribute name##_attr =	\
__ATTR(name##_ns, 0444, name##_show, NULL)

FPDT_ATTR(resume, resume_prev);
FPDT_ATTR(resume, resume_avg);
FPDT_ATTR(suspend, suspend_start);
FPDT_ATTR(suspend, suspend_end);
FPDT_ATTR(boot, firmware_start);
FPDT_ATTR(boot, bootloader_load);
FPDT_ATTR(boot, bootloader_launch);
FPDT_ATTR(boot, exitbootservice_start);
FPDT_ATTR(boot, exitbootservice_end);

static ssize_t resume_count_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", record_resume->resume_count);
}

static struct kobj_attribute resume_count_attr =
__ATTR_RO(resume_count);

static struct attribute *resume_attrs[] = {
	&resume_count_attr.attr,
	&resume_prev_attr.attr,
	&resume_avg_attr.attr,
	NULL
};

static const struct attribute_group resume_attr_group = {
	.attrs = resume_attrs,
	.name = "resume",
};

static struct attribute *suspend_attrs[] = {
	&suspend_start_attr.attr,
	&suspend_end_attr.attr,
	NULL
};

static const struct attribute_group suspend_attr_group = {
	.attrs = suspend_attrs,
	.name = "suspend",
};

static struct attribute *boot_attrs[] = {
	&firmware_start_attr.attr,
	&bootloader_load_attr.attr,
	&bootloader_launch_attr.attr,
	&exitbootservice_start_attr.attr,
	&exitbootservice_end_attr.attr,
	NULL
};

static const struct attribute_group boot_attr_group = {
	.attrs = boot_attrs,
	.name = "boot",
};

static struct kobject *fpdt_kobj;

static int fpdt_process_subtable(u64 address, u32 subtable_type)
{
	struct fpdt_subtable_header *subtable_header;
	struct fpdt_record_header *record_header;
	char *signature = (subtable_type == SUBTABLE_FBPT ? "FBPT" : "S3PT");
	u32 length, offset;
	int result;

	subtable_header = acpi_os_map_memory(address, sizeof(*subtable_header));
	if (!subtable_header)
		return -ENOMEM;

	if (strncmp((char *)&subtable_header->signature, signature, 4)) {
		pr_info(FW_BUG "subtable signature and type mismatch!\n");
		return -EINVAL;
	}

	length = subtable_header->length;
	acpi_os_unmap_memory(subtable_header, sizeof(*subtable_header));

	subtable_header = acpi_os_map_memory(address, length);
	if (!subtable_header)
		return -ENOMEM;

	offset = sizeof(*subtable_header);
	while (offset < length) {
		record_header = (void *)subtable_header + offset;
		offset += record_header->length;

		switch (record_header->type) {
		case RECORD_S3_RESUME:
			if (subtable_type != SUBTABLE_S3PT) {
				pr_err(FW_BUG "Invalid record %d for subtable %s\n",
				     record_header->type, signature);
				return -EINVAL;
			}
			if (record_resume) {
				pr_err("Duplicate resume performance record found.\n");
				continue;
			}
			record_resume = (struct resume_performance_record *)record_header;
			result = sysfs_create_group(fpdt_kobj, &resume_attr_group);
			if (result)
				return result;
			break;
		case RECORD_S3_SUSPEND:
			if (subtable_type != SUBTABLE_S3PT) {
				pr_err(FW_BUG "Invalid %d for subtable %s\n",
				     record_header->type, signature);
				continue;
			}
			if (record_suspend) {
				pr_err("Duplicate suspend performance record found.\n");
				continue;
			}
			record_suspend = (struct suspend_performance_record *)record_header;
			result = sysfs_create_group(fpdt_kobj, &suspend_attr_group);
			if (result)
				return result;
			break;
		case RECORD_BOOT:
			if (subtable_type != SUBTABLE_FBPT) {
				pr_err(FW_BUG "Invalid %d for subtable %s\n",
				     record_header->type, signature);
				return -EINVAL;
			}
			if (record_boot) {
				pr_err("Duplicate boot performance record found.\n");
				continue;
			}
			record_boot = (struct boot_performance_record *)record_header;
			result = sysfs_create_group(fpdt_kobj, &boot_attr_group);
			if (result)
				return result;
			break;

		default:
			/* Other types are reserved in ACPI 6.4 spec. */
			break;
		}
	}
	return 0;
}

static int __init acpi_init_fpdt(void)
{
	acpi_status status;
	struct acpi_table_header *header;
	struct fpdt_subtable_entry *subtable;
	u32 offset = sizeof(*header);

	status = acpi_get_table(ACPI_SIG_FPDT, 0, &header);

	if (ACPI_FAILURE(status))
		return 0;

	fpdt_kobj = kobject_create_and_add("fpdt", acpi_kobj);
	if (!fpdt_kobj) {
		acpi_put_table(header);
		return -ENOMEM;
	}

	while (offset < header->length) {
		subtable = (void *)header + offset;
		switch (subtable->type) {
		case SUBTABLE_FBPT:
		case SUBTABLE_S3PT:
			fpdt_process_subtable(subtable->address,
					      subtable->type);
			break;
		default:
			/* Other types are reserved in ACPI 6.4 spec. */
			break;
		}
		offset += sizeof(*subtable);
	}
	return 0;
}

fs_initcall(acpi_init_fpdt);
