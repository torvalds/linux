
/* initialisation functions */

extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
extern int firmware_init(void);
extern int platform_bus_init(void);
extern int system_bus_init(void);
extern int cpu_dev_init(void);
extern int attribute_container_init(void);

extern int bus_add_device(struct device * dev);
extern void bus_attach_device(struct device * dev);
extern void bus_remove_device(struct device * dev);
extern struct bus_type *get_bus(struct bus_type * bus);
extern void put_bus(struct bus_type * bus);

extern int bus_add_driver(struct device_driver *);
extern void bus_remove_driver(struct device_driver *);

extern void driver_detach(struct device_driver * drv);
extern int driver_probe_device(struct device_driver *, struct device *);

extern void sysdev_shutdown(void);
extern int sysdev_suspend(pm_message_t state);
extern int sysdev_resume(void);

static inline struct class_device *to_class_dev(struct kobject *obj)
{
	return container_of(obj, struct class_device, kobj);
}

static inline
struct class_device_attribute *to_class_dev_attr(struct attribute *_attr)
{
	return container_of(_attr, struct class_device_attribute, attr);
}


