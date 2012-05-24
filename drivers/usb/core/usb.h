#include <linux/pm.h>

/* Functions local to drivers/usb/core/ */

extern int usb_create_sysfs_dev_files(struct usb_device *dev);
extern void usb_remove_sysfs_dev_files(struct usb_device *dev);
extern void usb_create_sysfs_intf_files(struct usb_interface *intf);
extern void usb_remove_sysfs_intf_files(struct usb_interface *intf);
extern int usb_create_ep_devs(struct device *parent,
				struct usb_host_endpoint *endpoint,
				struct usb_device *udev);
extern void usb_remove_ep_devs(struct usb_host_endpoint *endpoint);

extern void usb_enable_endpoint(struct usb_device *dev,
		struct usb_host_endpoint *ep, bool reset_toggle);
extern void usb_enable_interface(struct usb_device *dev,
		struct usb_interface *intf, bool reset_toggles);
extern void usb_disable_endpoint(struct usb_device *dev, unsigned int epaddr,
		bool reset_hardware);
extern void usb_disable_interface(struct usb_device *dev,
		struct usb_interface *intf, bool reset_hardware);
extern void usb_release_interface_cache(struct kref *ref);
extern void usb_disable_device(struct usb_device *dev, int skip_ep0);
extern int usb_deauthorize_device(struct usb_device *);
extern int usb_authorize_device(struct usb_device *);
extern void usb_detect_quirks(struct usb_device *udev);
extern int usb_remove_device(struct usb_device *udev);

extern int usb_get_device_descriptor(struct usb_device *dev,
		unsigned int size);
extern int usb_get_bos_descriptor(struct usb_device *dev);
extern void usb_release_bos_descriptor(struct usb_device *dev);
extern char *usb_cache_string(struct usb_device *udev, int index);
extern int usb_set_configuration(struct usb_device *dev, int configuration);
extern int usb_choose_configuration(struct usb_device *udev);

extern void usb_kick_khubd(struct usb_device *dev);
extern int usb_match_device(struct usb_device *dev,
			    const struct usb_device_id *id);
extern void usb_forced_unbind_intf(struct usb_interface *intf);
extern void usb_rebind_intf(struct usb_interface *intf);

extern int usb_hub_claim_port(struct usb_device *hdev, unsigned port,
		void *owner);
extern int usb_hub_release_port(struct usb_device *hdev, unsigned port,
		void *owner);
extern void usb_hub_release_all_ports(struct usb_device *hdev, void *owner);
extern bool usb_device_is_owned(struct usb_device *udev);

extern int  usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);

#ifdef	CONFIG_PM

extern int usb_suspend(struct device *dev, pm_message_t msg);
extern int usb_resume(struct device *dev, pm_message_t msg);
extern int usb_resume_complete(struct device *dev);

extern int usb_port_suspend(struct usb_device *dev, pm_message_t msg);
extern int usb_port_resume(struct usb_device *dev, pm_message_t msg);

#else

static inline int usb_port_suspend(struct usb_device *udev, pm_message_t msg)
{
	return 0;
}

static inline int usb_port_resume(struct usb_device *udev, pm_message_t msg)
{
	return 0;
}

#endif

#ifdef CONFIG_USB_SUSPEND

extern void usb_autosuspend_device(struct usb_device *udev);
extern int usb_autoresume_device(struct usb_device *udev);
extern int usb_remote_wakeup(struct usb_device *dev);
extern int usb_runtime_suspend(struct device *dev);
extern int usb_runtime_resume(struct device *dev);
extern int usb_runtime_idle(struct device *dev);
extern int usb_set_usb2_hardware_lpm(struct usb_device *udev, int enable);

#else

#define usb_autosuspend_device(udev)		do {} while (0)
static inline int usb_autoresume_device(struct usb_device *udev)
{
	return 0;
}

static inline int usb_remote_wakeup(struct usb_device *udev)
{
	return 0;
}

static inline int usb_set_usb2_hardware_lpm(struct usb_device *udev, int enable)
{
	return 0;
}
#endif

extern struct bus_type usb_bus_type;
extern struct device_type usb_device_type;
extern struct device_type usb_if_device_type;
extern struct device_type usb_ep_device_type;
extern struct usb_device_driver usb_generic_driver;

static inline int is_usb_device(const struct device *dev)
{
	return dev->type == &usb_device_type;
}

static inline int is_usb_interface(const struct device *dev)
{
	return dev->type == &usb_if_device_type;
}

static inline int is_usb_endpoint(const struct device *dev)
{
	return dev->type == &usb_ep_device_type;
}

/* Do the same for device drivers and interface drivers. */

static inline int is_usb_device_driver(struct device_driver *drv)
{
	return container_of(drv, struct usbdrv_wrap, driver)->
			for_devices;
}

/* for labeling diagnostics */
extern const char *usbcore_name;

/* sysfs stuff */
extern const struct attribute_group *usb_device_groups[];
extern const struct attribute_group *usb_interface_groups[];

/* usbfs stuff */
extern struct mutex usbfs_mutex;
extern struct usb_driver usbfs_driver;
extern const struct file_operations usbfs_devices_fops;
extern const struct file_operations usbdev_file_operations;
extern void usbfs_conn_disc_event(void);

extern int usb_devio_init(void);
extern void usb_devio_cleanup(void);

/* internal notify stuff */
extern void usb_notify_add_device(struct usb_device *udev);
extern void usb_notify_remove_device(struct usb_device *udev);
extern void usb_notify_add_bus(struct usb_bus *ubus);
extern void usb_notify_remove_bus(struct usb_bus *ubus);

