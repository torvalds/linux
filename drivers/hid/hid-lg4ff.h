#ifndef __HID_LG4FF_H
#define __HID_LG4FF_H

#ifdef CONFIG_LOGIWHEELS_FF
extern int lg4ff_no_autoswitch; /* From hid-lg.c */

int lg4ff_adjust_input_event(struct hid_device *hid, struct hid_field *field,
			     struct hid_usage *usage, __s32 value, struct lg_drv_data *drv_data);
int lg4ff_init(struct hid_device *hdev);
int lg4ff_deinit(struct hid_device *hdev);
#else
static inline int lg4ff_adjust_input_event(struct hid_device *hid, struct hid_field *field,
					   struct hid_usage *usage, __s32 value, struct lg_drv_data *drv_data) { return 0; }
static inline int lg4ff_init(struct hid_device *hdev) { return -1; }
static inline int lg4ff_deinit(struct hid_device *hdev) { return -1; }
#endif

#endif
