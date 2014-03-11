#ifndef __RK3288_HDMI_H__
#define __RK3288_HDMI_H__

#include "../../rk_hdmi.h"


#define ENABLE		16
#define HDMI_SEL_LCDC(x)	((((x)&1)<<4)|(1<<(4+ENABLE)))


#endif /* __RK3288_HDMI_H__ */
