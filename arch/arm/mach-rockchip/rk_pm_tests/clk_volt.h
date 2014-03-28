#ifndef _CLK_VOLT_H_
#define _CLK_VOLT_H_
ssize_t clk_volt_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t clk_volt_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
