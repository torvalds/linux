/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_RETIMER__
#define __USB_TYPEC_RETIMER__

#include <linux/usb/typec_retimer.h>

struct typec_retimer {
	struct device dev;
	typec_retimer_set_fn_t set;
};

#define to_typec_retimer(_dev_) container_of(_dev_, struct typec_retimer, dev)

#endif /* __USB_TYPEC_RETIMER__ */
