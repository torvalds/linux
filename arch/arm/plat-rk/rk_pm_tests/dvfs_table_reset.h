#ifndef _DVFS_TABLE_RESET_H_
#define _DVFS_TABLE_RESET_H_
ssize_t dvfs_table_reset_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t dvfs_table_reset_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
