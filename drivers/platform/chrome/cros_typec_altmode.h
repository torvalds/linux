/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CROS_TYPEC_ALTMODE_H__
#define __CROS_TYPEC_ALTMODE_H__

#include <linux/kconfig.h>
#include <linux/usb/typec.h>

struct cros_typec_port;
struct typec_altmode;
struct typec_altmode_desc;
struct typec_displayport_data;

#if IS_ENABLED(CONFIG_TYPEC_DP_ALTMODE)
struct typec_altmode *
cros_typec_register_displayport(struct cros_typec_port *port,
				struct typec_altmode_desc *desc,
				bool ap_mode_entry);

int cros_typec_displayport_status_update(struct typec_altmode *altmode,
					 struct typec_displayport_data *data);
#else
static inline struct typec_altmode *
cros_typec_register_displayport(struct cros_typec_port *port,
				struct typec_altmode_desc *desc,
				bool ap_mode_entry)
{
	return typec_port_register_altmode(port->port, desc);
}

static inline int cros_typec_displayport_status_update(struct typec_altmode *altmode,
					 struct typec_displayport_data *data)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_TYPEC_TBT_ALTMODE)
struct typec_altmode *
cros_typec_register_thunderbolt(struct cros_typec_port *port,
				struct typec_altmode_desc *desc);
#else
static inline struct typec_altmode *
cros_typec_register_thunderbolt(struct cros_typec_port *port,
				struct typec_altmode_desc *desc)
{
	return typec_port_register_altmode(port->port, desc);
}
#endif

#endif /* __CROS_TYPEC_ALTMODE_H__ */
