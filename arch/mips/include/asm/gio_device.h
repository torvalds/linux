#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct gio_device_id {
	__u8 id;
};

struct gio_device {
	struct device	dev;
	struct resource resource;
	unsigned int	irq;
	unsigned int	slotno;

	const char	*name;
	struct gio_device_id id;
	unsigned	id32:1;
	unsigned	gio64:1;
};
#define to_gio_device(d) container_of(d, struct gio_device, dev)

struct gio_driver {
	const char    *name;
	struct module *owner;
	const struct gio_device_id *id_table;

	int  (*probe)(struct gio_device *, const struct gio_device_id *);
	void (*remove)(struct gio_device *);
	int  (*suspend)(struct gio_device *, pm_message_t);
	int  (*resume)(struct gio_device *);
	void (*shutdown)(struct gio_device *);

	struct device_driver driver;
};
#define to_gio_driver(drv) container_of(drv, struct gio_driver, driver)

extern const struct gio_device_id *gio_match_device(const struct gio_device_id *,
						    const struct gio_device *);
extern struct gio_device *gio_dev_get(struct gio_device *);
extern void gio_dev_put(struct gio_device *);

extern int gio_device_register(struct gio_device *);
extern void gio_device_unregister(struct gio_device *);
extern void gio_release_dev(struct device *);

static inline void gio_device_free(struct gio_device *dev)
{
	gio_release_dev(&dev->dev);
}

extern int gio_register_driver(struct gio_driver *);
extern void gio_unregister_driver(struct gio_driver *);

#define gio_get_drvdata(_dev)	     dev_get_drvdata(&(_dev)->dev)
#define gio_set_drvdata(_dev, data)  dev_set_drvdata(&(_dev)->dev, (data))

extern void gio_set_master(struct gio_device *);
