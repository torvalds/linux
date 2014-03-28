#ifndef _CPU_USAGE_H_
#define _CPU_USAGE_H_
ssize_t cpu_usage_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t cpu_usage_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
