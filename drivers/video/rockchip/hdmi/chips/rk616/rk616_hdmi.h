#ifndef __RK616_HDMI_H__
#define __RK616_HDMI_H__

#include "../../rk_hdmi.h"
#include <linux/mfd/rk616.h>

#if defined(CONFIG_HDMI_SOURCE_LCDC1)
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC1
#else
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC0
#endif
enum{
	INPUT_IIS,
	INPUT_SPDIF
};

#if defined(CONFIG_SND_RK_SOC_HDMI_SPDIF)
#define HDMI_CODEC_SOURCE_SELECT INPUT_SPDIF
#else
#define HDMI_CODEC_SOURCE_SELECT INPUT_IIS
#endif

extern void rk616_hdmi_control_output(int enable);
extern int rk616_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int  (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void));

struct rk616_hdmi {
        struct hdmi             g_hdmi;
        struct early_suspend    early_suspend;
        struct delayed_work     rk616_delay_work;
        struct work_struct      rk616_irq_work_struct;
        struct mfd_rk616        *rk616_drv;
        struct dentry           *debugfs_dir;
};

#endif /* __RK30_HDMI_H__ */
