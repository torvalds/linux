#ifndef _DELAYLINE_H_
#define _DELAYLINE_H_
ssize_t delayline_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t delayline_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
