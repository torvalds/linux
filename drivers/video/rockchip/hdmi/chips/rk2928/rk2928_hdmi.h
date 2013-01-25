#ifndef __RK30_HDMI_H__
#define __RK30_HDMI_H__

#include "../../rk_hdmi.h"

#if defined(CONFIG_HDMI_SOURCE_LCDC1)
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC1
#else
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC0
#endif

extern void	rk2928_hdmi_control_output(int enable);
extern int	rk2928_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int  (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void));
#endif /* __RK30_HDMI_H__ */
