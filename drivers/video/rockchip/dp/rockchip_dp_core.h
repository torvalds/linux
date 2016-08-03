#ifndef __ROCKCHIP_DP_CORE_H__
#define __ROCKCHIP_DP_CORE_H__

/* dp grf register offset */
#define DP_VOP_SEL		0x6224
#define DP_SEL_VOP_LIT		BIT(12)
#define MAX_FW_WAIT_SECS        64
#define CDN_DP_FIRMWARE         "cdn/dptx.bin"

#define EDID_BLOCK_SIZE		128

struct dp_disp_info {
	struct fb_videomode *mode;
	int color_depth;
	int vsync_polarity;
	int hsync_polarity;
	int vop_sel;
};

struct cdn_dp_data {
	u8 max_phy;
};

struct dp_dev {
	struct dp_disp_info disp_info;
	struct hdmi *hdmi;
	void *dp;
	struct notifier_block fb_notif;
	int lanes;
};

int cdn_dp_fb_register(struct platform_device *pdev, void *dp);
void hpd_change(struct device *dev, int lanes);

#endif
