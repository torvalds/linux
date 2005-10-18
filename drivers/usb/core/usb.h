/* Functions local to drivers/usb/core/ */

extern void usb_create_sysfs_dev_files (struct usb_device *dev);
extern void usb_remove_sysfs_dev_files (struct usb_device *dev);
extern void usb_create_sysfs_intf_files (struct usb_interface *intf);
extern void usb_remove_sysfs_intf_files (struct usb_interface *intf);

extern void usb_disable_endpoint (struct usb_device *dev, unsigned int epaddr);
extern void usb_disable_interface (struct usb_device *dev,
		struct usb_interface *intf);
extern void usb_release_interface_cache(struct kref *ref);
extern void usb_disable_device (struct usb_device *dev, int skip_ep0);

extern int usb_get_device_descriptor(struct usb_device *dev,
		unsigned int size);
extern int usb_set_configuration(struct usb_device *dev, int configuration);

extern void usb_lock_all_devices(void);
extern void usb_unlock_all_devices(void);

extern void usb_kick_khubd(struct usb_device *dev);
extern void usb_resume_root_hub(struct usb_device *dev);

extern int  usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);
extern int usb_host_init(void);
extern void usb_host_cleanup(void);

/* for labeling diagnostics */
extern const char *usbcore_name;

/* usbfs stuff */
extern struct usb_driver usbfs_driver;
extern struct file_operations usbfs_devices_fops;
extern struct file_operations usbfs_device_file_operations;
extern void usbfs_conn_disc_event(void);

extern int usbdev_init(void);
extern void usbdev_cleanup(void);
extern void usbdev_add(struct usb_device *dev);
extern void usbdev_remove(struct usb_device *dev);
extern struct usb_device *usbdev_lookup_minor(int minor);

struct dev_state {
	struct list_head list;      /* state list */
	struct usb_device *dev;
	struct file *file;
	spinlock_t lock;            /* protects the async urb lists */
	struct list_head async_pending;
	struct list_head async_completed;
	wait_queue_head_t wait;     /* wake up if a request completed */
	unsigned int discsignr;
	pid_t disc_pid;
	uid_t disc_uid, disc_euid;
	void __user *disccontext;
	unsigned long ifclaimed;
};

