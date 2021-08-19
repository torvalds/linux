/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_MUX__
#define __USB_TYPEC_MUX__

#include <linux/usb/typec_mux.h>

struct typec_switch {
	struct device dev;
	typec_switch_set_fn_t set;
};

struct typec_mux {
	struct device dev;
	typec_mux_set_fn_t set;
};

#define to_typec_switch(_dev_) container_of(_dev_, struct typec_switch, dev)
#define to_typec_mux(_dev_) container_of(_dev_, struct typec_mux, dev)

#endif /* __USB_TYPEC_MUX__ */
