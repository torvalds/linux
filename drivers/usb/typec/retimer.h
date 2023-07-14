/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_RETIMER__
#define __USB_TYPEC_RETIMER__

#include <linux/usb/typec_retimer.h>

struct typec_retimer {
	struct device dev;
	typec_retimer_set_fn_t set;
};

#define to_typec_retimer(_dev_) container_of(_dev_, struct typec_retimer, dev)

extern const struct device_type typec_retimer_dev_type;

#define is_typec_retimer(dev) ((dev)->type == &typec_retimer_dev_type)

#endif /* __USB_TYPEC_RETIMER__ */
