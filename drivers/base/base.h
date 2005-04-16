extern int bus_add_device(struct device * dev);
extern void bus_remove_device(struct device * dev);

extern int bus_add_driver(struct device_driver *);
extern void bus_remove_driver(struct device_driver *);

static inline struct class_device *to_class_dev(struct kobject *obj)
{
	return container_of(obj, struct class_device, kobj);
}

static inline
struct class_device_attribute *to_class_dev_attr(struct attribute *_attr)
{
	return container_of(_attr, struct class_device_attribute, attr);
}


