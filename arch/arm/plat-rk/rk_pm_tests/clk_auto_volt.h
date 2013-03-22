#ifndef _CLK_AUTO_VOLT_H_
#define _CLK_AUTO_VOLT_H_
ssize_t clk_auto_volt_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
ssize_t clk_auto_volt_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n);

#endif
