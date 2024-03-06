/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_CLASS__
#define __USB_TYPEC_CLASS__

#include <linux/device.h>
#include <linux/usb/typec.h>

struct typec_mux;
struct typec_switch;
struct usb_device;

struct typec_plug {
	struct device			dev;
	enum typec_plug_index		index;
	struct ida			mode_ids;
	int				num_altmodes;
};

struct typec_cable {
	struct device			dev;
	enum typec_plug_type		type;
	struct usb_pd_identity		*identity;
	unsigned int			active:1;
	u16				pd_revision; /* 0300H = "3.0" */
};

struct typec_partner {
	struct device			dev;
	unsigned int			usb_pd:1;
	struct usb_pd_identity		*identity;
	enum typec_accessory		accessory;
	struct ida			mode_ids;
	int				num_altmodes;
	u16				pd_revision; /* 0300H = "3.0" */
	enum usb_pd_svdm_ver		svdm_version;

	struct usb_power_delivery	*pd;

	void (*attach)(struct typec_partner *partner, struct device *dev);
	void (*deattach)(struct typec_partner *partner, struct device *dev);
};

struct typec_port {
	unsigned int			id;
	struct device			dev;
	struct ida			mode_ids;

	struct usb_power_delivery	*pd;

	int				prefer_role;
	enum typec_data_role		data_role;
	enum typec_role			pwr_role;
	enum typec_role			vconn_role;
	enum typec_pwr_opmode		pwr_opmode;
	enum typec_port_type		port_type;
	struct mutex			port_type_lock;

	enum typec_orientation		orientation;
	struct typec_switch		*sw;
	struct typec_mux		*mux;
	struct typec_retimer		*retimer;

	const struct typec_capability	*cap;
	const struct typec_operations   *ops;

	struct typec_connector		con;

	/*
	 * REVISIT: Only USB devices for now. If there are others, these need to
	 * be converted into a list.
	 *
	 * NOTE: These may be registered first before the typec_partner, so they
	 * will always have to be kept here instead of struct typec_partner.
	 */
	struct device			*usb2_dev;
	struct device			*usb3_dev;
};

#define to_typec_port(_dev_) container_of(_dev_, struct typec_port, dev)
#define to_typec_plug(_dev_) container_of(_dev_, struct typec_plug, dev)
#define to_typec_cable(_dev_) container_of(_dev_, struct typec_cable, dev)
#define to_typec_partner(_dev_) container_of(_dev_, struct typec_partner, dev)

extern const struct device_type typec_partner_dev_type;
extern const struct device_type typec_cable_dev_type;
extern const struct device_type typec_plug_dev_type;
extern const struct device_type typec_port_dev_type;

#define is_typec_partner(dev) ((dev)->type == &typec_partner_dev_type)
#define is_typec_cable(dev) ((dev)->type == &typec_cable_dev_type)
#define is_typec_plug(dev) ((dev)->type == &typec_plug_dev_type)
#define is_typec_port(dev) ((dev)->type == &typec_port_dev_type)

extern struct class typec_mux_class;
extern struct class retimer_class;
extern struct class typec_class;

#if defined(CONFIG_ACPI)
int typec_link_ports(struct typec_port *connector);
void typec_unlink_ports(struct typec_port *connector);
#else
static inline int typec_link_ports(struct typec_port *connector) { return 0; }
static inline void typec_unlink_ports(struct typec_port *connector) { }
#endif

#endif /* __USB_TYPEC_CLASS__ */
