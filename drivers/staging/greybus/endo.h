/*
 * Greybus endo code
 *
 * Copyright 2015 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __ENDO_H
#define __ENDO_H

/* Greybus "public" definitions" */
struct gb_svc {
	u8 serial_number[10];
	u8 version[10];
};

struct gb_endo {
	struct device dev;
	struct gb_svc svc;
	u16 id;
};
#define to_gb_endo(d) container_of(d, struct gb_endo, dev)


/* Greybus "private" definitions */
struct greybus_host_device;

struct gb_endo *gb_endo_create(struct greybus_host_device *hd);
void gb_endo_remove(struct gb_endo *endo);

#endif /* __ENDO_H */
