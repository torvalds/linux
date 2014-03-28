#ifndef _DVFS_TABLE_SCAN_H_
#define _DVFS_TABLE_SCAN_H_
ssize_t dvfs_table_scan_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t dvfs_table_scan_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
