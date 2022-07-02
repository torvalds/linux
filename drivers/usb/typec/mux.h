/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_MUX__
#define __USB_TYPEC_MUX__

#include <linux/usb/typec_mux.h>

struct typec_switch_dev {
	struct device dev;
	typec_switch_set_fn_t set;
};

struct typec_mux_dev {
	struct device dev;
	typec_mux_set_fn_t set;
};

#define to_typec_switch_dev(_dev_) container_of(_dev_, struct typec_switch_dev, dev)
#define to_typec_mux_dev(_dev_) container_of(_dev_, struct typec_mux_dev, dev)

extern const struct device_type typec_switch_dev_type;
extern const struct device_type typec_mux_dev_type;

#define is_typec_switch_dev(dev) ((dev)->type == &typec_switch_dev_type)
#define is_typec_mux_dev(dev) ((dev)->type == &typec_mux_dev_type)

#endif /* __USB_TYPEC_MUX__ */
