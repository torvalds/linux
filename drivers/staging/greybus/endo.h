/*
 * Greybus endo code
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __ENDO_H
#define __ENDO_H

/* Greybus "public" definitions" */
struct gb_svc_info {
	u8 serial_number[10];
	u8 version[10];
};

/* Max ribs per Endo size */
#define ENDO_BACK_RIBS_MINI		0x4
#define ENDO_BACK_RIBS_MEDIUM		0x5
#define ENDO_BACK_RIBS_LARGE		0x6

/**
 * struct endo_layout - represents front/back ribs of the endo.
 *
 * @front_ribs:	Mask of present ribs in front.
 * @left_ribs:	Mask of present ribs in back (left).
 * @right_ribs:	Mask of present ribs in back (right).
 * @max_ribs:	Max ribs on endo back, possible values defined above.
 */
struct endo_layout {
	u8	front_ribs;
	u8	left_ribs;
	u8	right_ribs;
	u8	max_ribs;
};

struct gb_endo {
	struct device dev;
	struct endo_layout layout;
	struct gb_svc_info svc_info;
	u16 dev_id;
	u16 id;
	u8 ap_intf_id;
};
#define to_gb_endo(d) container_of(d, struct gb_endo, dev)

extern struct ida greybus_endo_id_map;

/* Greybus "private" definitions */
struct greybus_host_device;

struct gb_endo *gb_endo_create(struct greybus_host_device *hd,
				u16 endo_id, u8 ap_intf_id);
void gb_endo_remove(struct gb_endo *endo);

u8 endo_get_module_id(struct gb_endo *endo, u8 interface_id);

#endif /* __ENDO_H */
