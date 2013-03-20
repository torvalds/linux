#ifndef _CLK_RATE_H_
#define _CLK_RATE_H_
ssize_t clk_rate_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t clk_rate_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
