#ifndef __RK30_HDMI_H__
#define __RK30_HDMI_H__

#include "../../rk_hdmi.h"


extern int rk30_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int  (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void));
#endif /* __RK30_HDMI_H__ */
