// SPDX-License-Identifier: GPL-2.0
/*
 * Functions corresponding to sure start object type attributes under
 * BIOS for use with hp-bioscfg driver
 *
 * Copyright (c) 2022 HP Development Company, L.P.
 */

#include "bioscfg.h"
#include <linux/types.h>

/* Maximum number of log entries supported when log entry size is 16
 * bytes. This value is calculated by dividing 4096 (page size) by
 * log entry size.
 */
#define LOG_MAX_ENTRIES		254

/*
 * Current Log entry size. This value size will change in the
 * future. The driver reads a total of 128 bytes for each log entry
 * provided by BIOS but only the first 16 bytes are used/read.
 */
#define LOG_ENTRY_SIZE		16

/*
 * audit_log_entry_count_show - Reports the number of
 *				existing audit log entries available
 *				to be read
 */
static ssize_t audit_log_entry_count_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	int ret;
	u32 count = 0;

	ret = hp_wmi_perform_query(HPWMI_SURESTART_GET_LOG_COUNT,
				   HPWMI_SURESTART,
				   &count, 1, sizeof(count));

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d,%d,%d\n", count, LOG_ENTRY_SIZE,
			  LOG_MAX_ENTRIES);
}

/*
 * audit_log_entries_show() - Return all entries found in log file
 */
static ssize_t audit_log_entries_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	int ret;
	int i;
	u32 count = 0;
	u8 audit_log_buffer[128];

	// Get the number of event logs
	ret = hp_wmi_perform_query(HPWMI_SURESTART_GET_LOG_COUNT,
				   HPWMI_SURESTART,
				   &count, 1, sizeof(count));

	if (ret < 0)
		return ret;

	/*
	 * The show() api will not work if the audit logs ever go
	 * beyond 4KB
	 */
	if (count * LOG_ENTRY_SIZE > PAGE_SIZE)
		return -EIO;

	/*
	 * We are guaranteed the buffer is 4KB so today all the event
	 * logs will fit
	 */
	for (i = 0; i < count; i++) {
		audit_log_buffer[0] = i + 1;

		/*
		 * read audit log entry at a time. 'buf' input value
		 * provides the audit log entry to be read. On
		 * input, Byte 0 = Audit Log entry number from
		 * beginning (1..254)
		 * Entry number 1 is the newest entry whereas the
		 * highest entry number (number of entries) is the
		 * oldest entry.
		 */
		ret = hp_wmi_perform_query(HPWMI_SURESTART_GET_LOG,
					   HPWMI_SURESTART,
					   audit_log_buffer, 1, 128);

		if (ret < 0 || (LOG_ENTRY_SIZE * i) > PAGE_SIZE) {
			/*
			 * Encountered a failure while reading
			 * individual logs. Only a partial list of
			 * audit log will be returned.
			 */
			break;
		} else {
			memcpy(buf, audit_log_buffer, LOG_ENTRY_SIZE);
			buf += LOG_ENTRY_SIZE;
		}
	}

	return i * LOG_ENTRY_SIZE;
}

static struct kobj_attribute sure_start_audit_log_entry_count = __ATTR_RO(audit_log_entry_count);
static struct kobj_attribute sure_start_audit_log_entries = __ATTR_RO(audit_log_entries);

static struct attribute *sure_start_attrs[] = {
	&sure_start_audit_log_entry_count.attr,
	&sure_start_audit_log_entries.attr,
	NULL
};

static const struct attribute_group sure_start_attr_group = {
	.attrs = sure_start_attrs,
};

void hp_exit_sure_start_attributes(void)
{
	sysfs_remove_group(bioscfg_drv.sure_start_attr_kobj,
			   &sure_start_attr_group);
}

int hp_populate_sure_start_data(struct kobject *attr_name_kobj)
{
	bioscfg_drv.sure_start_attr_kobj = attr_name_kobj;
	return sysfs_create_group(attr_name_kobj, &sure_start_attr_group);
}
