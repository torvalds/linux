/*
 * Greybus functions
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __FUNCTION_H
#define __FUNCTION_H

struct gb_function {
	struct gb_interface		*interface;
	u16				cport_id;
	enum greybus_function_type	type;

	struct list_head		links;	/* interface->functions */
};

struct gb_function *gb_function_create(struct gb_interface *interface,
				u16 cport_id,
				enum greybus_function_type function_type);
void gb_function_destroy(struct gb_function *function);

#endif /* __FUNCTION_H */
