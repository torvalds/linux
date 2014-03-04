#ifndef __RK3288_HDMI_H__
#define __RK3288_HDMI_H__

#include "../../rk_hdmi.h"

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

struct rk_hdmi_device {
        struct rk_hdmi_driver	hdmi_drv;
        struct delayed_work     hdmi_delay_work;
        struct work_struct      hdmi_irq_work_struct;
        struct dentry           *debugfs_dir;
};

#endif /* __RK3288_HDMI_H__ */
