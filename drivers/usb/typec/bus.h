/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_ALTMODE_H__
#define __USB_TYPEC_ALTMODE_H__

#include <linux/usb/typec_altmode.h>

struct bus_type;
struct typec_mux;
struct typec_retimer;

struct altmode {
	unsigned int			id;
	struct typec_altmode		adev;
	struct typec_mux		*mux;
	struct typec_retimer		*retimer;

	enum typec_port_data		roles;

	struct attribute		*attrs[5];
	char				group_name[8];
	struct attribute_group		group;
	const struct attribute_group	*groups[2];

	struct altmode			*partner;
	struct altmode			*plug[2];
};

#define to_altmode(d) container_of(d, struct altmode, adev)

extern struct bus_type typec_bus;
extern const struct device_type typec_altmode_dev_type;

#define is_typec_altmode(_dev_) (_dev_->type == &typec_altmode_dev_type)

#endif /* __USB_TYPEC_ALTMODE_H__ */
