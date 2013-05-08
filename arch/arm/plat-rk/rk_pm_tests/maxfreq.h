#ifndef _MAXFREQ_H_
#define _MAXFREQ_H_
ssize_t maxfreq_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t maxfreq_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
