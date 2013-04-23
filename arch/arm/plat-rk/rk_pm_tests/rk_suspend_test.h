#ifndef __AUTO_WAKEUP_H 
#define __AUTO_WAKEUP_H 

ssize_t auto_wakeup_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t auto_wakeup_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);
ssize_t suspend_test_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);
ssize_t suspend_test_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);


#endif

