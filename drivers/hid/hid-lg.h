#ifndef __HID_LG_H
#define __HID_LG_H

struct lg_drv_data {
	unsigned long quirks;
	void *device_props;	/* Device specific properties */
};

#ifdef CONFIG_LOGITECH_FF
int lgff_init(struct hid_device *hdev);
#else
static inline int lgff_init(struct hid_device *hdev) { return -1; }
#endif

#ifdef CONFIG_LOGIRUMBLEPAD2_FF
int lg2ff_init(struct hid_device *hdev);
#else
static inline int lg2ff_init(struct hid_device *hdev) { return -1; }
#endif

#ifdef CONFIG_LOGIG940_FF
int lg3ff_init(struct hid_device *hdev);
#else
static inline int lg3ff_init(struct hid_device *hdev) { return -1; }
#endif

#ifdef CONFIG_LOGIWHEELS_FF
int lg4ff_init(struct hid_device *hdev);
int lg4ff_deinit(struct hid_device *hdev);
#else
static inline int lg4ff_init(struct hid_device *hdev) { return -1; }
static inline int lg4ff_deinit(struct hid_device *hdev) { return -1; }
#endif

#endif
