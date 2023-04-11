/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CROS_EC_TYPEC__
#define __CROS_EC_TYPEC__

#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/usb/pd.h>
#include <linux/usb/role.h>
#include <linux/usb/typec.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>
#include <linux/workqueue.h>

/* Supported alt modes. */
enum {
	CROS_EC_ALTMODE_DP = 0,
	CROS_EC_ALTMODE_TBT,
	CROS_EC_ALTMODE_MAX,
};

/* Container for altmode pointer nodes. */
struct cros_typec_altmode_node {
	struct typec_altmode *amode;
	struct list_head list;
};

/* Platform-specific data for the Chrome OS EC Type C controller. */
struct cros_typec_data {
	struct device *dev;
	struct cros_ec_device *ec;
	int num_ports;
	unsigned int pd_ctrl_ver;
	/* Array of ports, indexed by port number. */
	struct cros_typec_port *ports[EC_USB_PD_MAX_PORTS];
	struct notifier_block nb;
	struct work_struct port_work;
	bool typec_cmd_supported;
	bool needs_mux_ack;
};

/* Per port data. */
struct cros_typec_port {
	struct typec_port *port;
	int port_num;
	/* Initial capabilities for the port. */
	struct typec_capability caps;
	struct typec_partner *partner;
	struct typec_cable *cable;
	/* SOP' plug. */
	struct typec_plug *plug;
	/* Port partner PD identity info. */
	struct usb_pd_identity p_identity;
	/* Port cable PD identity info. */
	struct usb_pd_identity c_identity;
	struct typec_switch *ori_sw;
	struct typec_mux *mux;
	struct typec_retimer *retimer;
	struct usb_role_switch *role_sw;

	/* Variables keeping track of switch state. */
	struct typec_mux_state state;
	uint8_t mux_flags;
	uint8_t role;

	struct typec_altmode *port_altmode[CROS_EC_ALTMODE_MAX];

	/* Flag indicating that PD partner discovery data parsing is completed. */
	bool sop_disc_done;
	bool sop_prime_disc_done;
	struct ec_response_typec_discovery *disc_data;
	struct list_head partner_mode_list;
	struct list_head plug_mode_list;

	/* PDO-related structs */
	struct usb_power_delivery *partner_pd;
	struct usb_power_delivery_capabilities *partner_src_caps;
	struct usb_power_delivery_capabilities *partner_sink_caps;

	struct cros_typec_data *typec_data;
};

#endif /*  __CROS_EC_TYPEC__ */
