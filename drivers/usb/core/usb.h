/* Functions local to drivers/usb/core/ */

extern int usb_create_sysfs_dev_files(struct usb_device *dev);
extern void usb_remove_sysfs_dev_files(struct usb_device *dev);
extern int usb_create_sysfs_intf_files(struct usb_interface *intf);
extern void usb_remove_sysfs_intf_files(struct usb_interface *intf);
extern int usb_create_ep_files(struct device *parent,
				struct usb_host_endpoint *endpoint,
				struct usb_device *udev);
extern void usb_remove_ep_files(struct usb_host_endpoint *endpoint);

extern void usb_enable_endpoint(struct usb_device *dev,
		struct usb_host_endpoint *ep);
extern void usb_disable_endpoint(struct usb_device *dev, unsigned int epaddr);
extern void usb_disable_interface(struct usb_device *dev,
		struct usb_interface *intf);
extern void usb_release_interface_cache(struct kref *ref);
extern void usb_disable_device(struct usb_device *dev, int skip_ep0);
extern int usb_deauthorize_device(struct usb_device *);
extern int usb_authorize_device(struct usb_device *);
extern void usb_detect_quirks(struct usb_device *udev);

extern int usb_get_device_descriptor(struct usb_device *dev,
		unsigned int size);
extern char *usb_cache_string(struct usb_device *udev, int index);
extern int usb_set_configuration(struct usb_device *dev, int configuration);
extern int usb_choose_configuration(struct usb_device *udev);

extern void usb_kick_khubd(struct usb_device *dev);
extern int usb_match_device(struct usb_device *dev,
			    const struct usb_device_id *id);

extern int  usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);
extern int usb_host_init(void);
extern void usb_host_cleanup(void);

#ifdef	CONFIG_PM

extern void usb_autosuspend_work(struct work_struct *work);
extern int usb_port_suspend(struct usb_device *dev);
extern int usb_port_resume(struct usb_device *dev);
extern int usb_external_suspend_device(struct usb_device *udev,
		pm_message_t msg);
extern int usb_external_resume_device(struct usb_device *udev);

static inline void usb_pm_lock(struct usb_device *udev)
{
	mutex_lock_nested(&udev->pm_mutex, udev->level);
}

static inline void usb_pm_unlock(struct usb_device *udev)
{
	mutex_unlock(&udev->pm_mutex);
}

#else

static inline int usb_port_suspend(struct usb_device *udev)
{
	return 0;
}

static inline int usb_port_resume(struct usb_device *udev)
{
	return 0;
}

static inline void usb_pm_lock(struct usb_device *udev) {}
static inline void usb_pm_unlock(struct usb_device *udev) {}

#endif

#ifdef CONFIG_USB_SUSPEND

extern void usb_autosuspend_device(struct usb_device *udev);
extern void usb_try_autosuspend_device(struct usb_device *udev);
extern int usb_autoresume_device(struct usb_device *udev);

#else

#define usb_autosuspend_device(udev)		do {} while (0)
#define usb_try_autosuspend_device(udev)	do {} while (0)
static inline int usb_autoresume_device(struct usb_device *udev)
{
	return 0;
}

#endif

extern struct workqueue_struct *ksuspend_usb_wq;
extern struct bus_type usb_bus_type;
extern struct device_type usb_device_type;
extern struct device_type usb_if_device_type;
extern struct usb_device_driver usb_generic_driver;

static inline int is_usb_device(const struct device *dev)
{
	return dev->type == &usb_device_type;
}

/* Do the same for device drivers and interface drivers. */

static inline int is_usb_device_driver(struct device_driver *drv)
{
	return container_of(drv, struct usbdrv_wrap, driver)->
			for_devices;
}

/* Interfaces and their "power state" are owned by usbcore */

static inline void mark_active(struct usb_interface *f)
{
	f->is_active = 1;
}

static inline void mark_quiesced(struct usb_interface *f)
{
	f->is_active = 0;
}

static inline int is_active(const struct usb_interface *f)
{
	return f->is_active;
}


/* for labeling diagnostics */
extern const char *usbcore_name;

/* sysfs stuff */
extern struct attribute_group *usb_device_groups[];
extern struct attribute_group *usb_interface_groups[];

/* usbfs stuff */
extern struct mutex usbfs_mutex;
extern struct usb_driver usbfs_driver;
extern const struct file_operations usbfs_devices_fops;
extern const struct file_operations usbdev_file_operations;
extern void usbfs_conn_disc_event(void);

extern int usb_devio_init(void);
extern void usb_devio_cleanup(void);

struct dev_state {
	struct list_head list;      /* state list */
	struct usb_device *dev;
	struct file *file;
	spinlock_t lock;            /* protects the async urb lists */
	struct list_head async_pending;
	struct list_head async_completed;
	wait_queue_head_t wait;     /* wake up if a request completed */
	unsigned int discsignr;
	struct pid *disc_pid;
	uid_t disc_uid, disc_euid;
	void __user *disccontext;
	unsigned long ifclaimed;
	u32 secid;
};

/* internal notify stuff */
extern void usb_notify_add_device(struct usb_device *udev);
extern void usb_notify_remove_device(struct usb_device *udev);
extern void usb_notify_add_bus(struct usb_bus *ubus);
extern void usb_notify_remove_bus(struct usb_bus *ubus);

