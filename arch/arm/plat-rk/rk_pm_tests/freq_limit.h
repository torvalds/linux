#ifndef _FREQ_LIMIT_H_
#define _FREQ_LIMIT_H_
ssize_t freq_limit_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t freq_limit_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
