/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CROS_TYPEC_VDM__
#define __CROS_TYPEC_VDM__

#include <linux/usb/typec_altmode.h>

extern const struct typec_altmode_ops port_amode_ops;

void cros_typec_handle_vdm_attention(struct cros_typec_data *typec, int port_num);
void cros_typec_handle_vdm_response(struct cros_typec_data *typec, int port_num);

#endif /*  __CROS_TYPEC_VDM__ */
