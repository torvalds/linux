#ifndef __RK3288_HDMI_H__
#define __RK3288_HDMI_H__

#include "../../rk_hdmi.h"


#define ENABLE		16
#define HDMI_SEL_LCDC(x)	((((x)&1)<<4)|(1<<(4+ENABLE)))

extern irqreturn_t hdmi_irq(int irq, void *priv);
extern struct hdmi *rk3288_hdmi_register_hdcp_callbacks(
					 void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int  (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void));


#endif /* __RK3288_HDMI_H__ */
