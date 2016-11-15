#ifndef __ROCKCHIP_DP_CORE_H__
#define __ROCKCHIP_DP_CORE_H__

/* dp grf register offset */
#define GRF_SOC_CON9		0x6224
#define GRF_SOC_CON26		0x6268

#define DPTX_HPD_SEL		(3 << 12)
#define DPTX_HPD_DEL		(2 << 12)
#define DPTX_HPD_SEL_MASK	(3 << 28)

#define DP_SEL_VOP_LIT		BIT(12)
#define MAX_FW_WAIT_SECS	64
#define EDID_BLOCK_SIZE		128
#define CDN_DP_FIRMWARE	"cdn/dptx.bin"

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
	bool early_suspended;
};

int cdn_dp_fb_register(struct platform_device *pdev, void *dp);
void hpd_change(struct device *dev, int lanes);

#endif
