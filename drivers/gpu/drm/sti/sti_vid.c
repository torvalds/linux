/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <linux/seq_file.h>

#include <drm/drmP.h>

#include "sti_plane.h"
#include "sti_vid.h"
#include "sti_vtg.h"

/* Registers */
#define VID_CTL                 0x00
#define VID_ALP                 0x04
#define VID_CLF                 0x08
#define VID_VPO                 0x0C
#define VID_VPS                 0x10
#define VID_KEY1                0x28
#define VID_KEY2                0x2C
#define VID_MPR0                0x30
#define VID_MPR1                0x34
#define VID_MPR2                0x38
#define VID_MPR3                0x3C
#define VID_MST                 0x68
#define VID_BC                  0x70
#define VID_TINT                0x74
#define VID_CSAT                0x78

/* Registers values */
#define VID_CTL_IGNORE          (BIT(31) | BIT(30))
#define VID_CTL_PSI_ENABLE      (BIT(2) | BIT(1) | BIT(0))
#define VID_ALP_OPAQUE          0x00000080
#define VID_BC_DFLT             0x00008000
#define VID_TINT_DFLT           0x00000000
#define VID_CSAT_DFLT           0x00000080
/* YCbCr to RGB BT709:
 * R = Y+1.5391Cr
 * G = Y-0.4590Cr-0.1826Cb
 * B = Y+1.8125Cb */
#define VID_MPR0_BT709          0x0A800000
#define VID_MPR1_BT709          0x0AC50000
#define VID_MPR2_BT709          0x07150545
#define VID_MPR3_BT709          0x00000AE8
/* YCbCr to RGB BT709:
 * R = Y+1.3711Cr
 * G = Y-0.6992Cr-0.3359Cb
 * B = Y+1.7344Cb
 */
#define VID_MPR0_BT601          0x0A800000
#define VID_MPR1_BT601          0x0AAF0000
#define VID_MPR2_BT601          0x094E0754
#define VID_MPR3_BT601          0x00000ADD

#define VID_MIN_HD_HEIGHT       720

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   readl(vid->regs + reg))

static void vid_dbg_ctl(struct seq_file *s, int val)
{
	val = val >> 30;
	seq_puts(s, "\t");

	if (!(val & 1))
		seq_puts(s, "NOT ");
	seq_puts(s, "ignored on main mixer - ");

	if (!(val & 2))
		seq_puts(s, "NOT ");
	seq_puts(s, "ignored on aux mixer");
}

static void vid_dbg_vpo(struct seq_file *s, int val)
{
	seq_printf(s, "\txdo:%4d\tydo:%4d", val & 0x0FFF, (val >> 16) & 0x0FFF);
}

static void vid_dbg_vps(struct seq_file *s, int val)
{
	seq_printf(s, "\txds:%4d\tyds:%4d", val & 0x0FFF, (val >> 16) & 0x0FFF);
}

static void vid_dbg_mst(struct seq_file *s, int val)
{
	if (val & 1)
		seq_puts(s, "\tBUFFER UNDERFLOW!");
}

static int vid_dbg_show(struct seq_file *s, void *arg)
{
	struct drm_info_node *node = s->private;
	struct sti_vid *vid = (struct sti_vid *)node->info_ent->data;

	seq_printf(s, "VID: (vaddr= 0x%p)", vid->regs);

	DBGFS_DUMP(VID_CTL);
	vid_dbg_ctl(s, readl(vid->regs + VID_CTL));
	DBGFS_DUMP(VID_ALP);
	DBGFS_DUMP(VID_CLF);
	DBGFS_DUMP(VID_VPO);
	vid_dbg_vpo(s, readl(vid->regs + VID_VPO));
	DBGFS_DUMP(VID_VPS);
	vid_dbg_vps(s, readl(vid->regs + VID_VPS));
	DBGFS_DUMP(VID_KEY1);
	DBGFS_DUMP(VID_KEY2);
	DBGFS_DUMP(VID_MPR0);
	DBGFS_DUMP(VID_MPR1);
	DBGFS_DUMP(VID_MPR2);
	DBGFS_DUMP(VID_MPR3);
	DBGFS_DUMP(VID_MST);
	vid_dbg_mst(s, readl(vid->regs + VID_MST));
	DBGFS_DUMP(VID_BC);
	DBGFS_DUMP(VID_TINT);
	DBGFS_DUMP(VID_CSAT);
	seq_puts(s, "\n");

	return 0;
}

static struct drm_info_list vid_debugfs_files[] = {
	{ "vid", vid_dbg_show, 0, NULL },
};

int vid_debugfs_init(struct sti_vid *vid, struct drm_minor *minor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vid_debugfs_files); i++)
		vid_debugfs_files[i].data = vid;

	return drm_debugfs_create_files(vid_debugfs_files,
					ARRAY_SIZE(vid_debugfs_files),
					minor->debugfs_root, minor);
}

void sti_vid_commit(struct sti_vid *vid,
		    struct drm_plane_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_display_mode *mode = &crtc->mode;
	int dst_x = state->crtc_x;
	int dst_y = state->crtc_y;
	int dst_w = clamp_val(state->crtc_w, 0, mode->crtc_hdisplay - dst_x);
	int dst_h = clamp_val(state->crtc_h, 0, mode->crtc_vdisplay - dst_y);
	int src_h = state->src_h >> 16;
	u32 val, ydo, xdo, yds, xds;

	/* Input / output size
	 * Align to upper even value */
	dst_w = ALIGN(dst_w, 2);
	dst_h = ALIGN(dst_h, 2);

	/* Unmask */
	val = readl(vid->regs + VID_CTL);
	val &= ~VID_CTL_IGNORE;
	writel(val, vid->regs + VID_CTL);

	ydo = sti_vtg_get_line_number(*mode, dst_y);
	yds = sti_vtg_get_line_number(*mode, dst_y + dst_h - 1);
	xdo = sti_vtg_get_pixel_number(*mode, dst_x);
	xds = sti_vtg_get_pixel_number(*mode, dst_x + dst_w - 1);

	writel((ydo << 16) | xdo, vid->regs + VID_VPO);
	writel((yds << 16) | xds, vid->regs + VID_VPS);

	/* Color conversion parameters */
	if (src_h >= VID_MIN_HD_HEIGHT) {
		writel(VID_MPR0_BT709, vid->regs + VID_MPR0);
		writel(VID_MPR1_BT709, vid->regs + VID_MPR1);
		writel(VID_MPR2_BT709, vid->regs + VID_MPR2);
		writel(VID_MPR3_BT709, vid->regs + VID_MPR3);
	} else {
		writel(VID_MPR0_BT601, vid->regs + VID_MPR0);
		writel(VID_MPR1_BT601, vid->regs + VID_MPR1);
		writel(VID_MPR2_BT601, vid->regs + VID_MPR2);
		writel(VID_MPR3_BT601, vid->regs + VID_MPR3);
	}
}

void sti_vid_disable(struct sti_vid *vid)
{
	u32 val;

	/* Mask */
	val = readl(vid->regs + VID_CTL);
	val |= VID_CTL_IGNORE;
	writel(val, vid->regs + VID_CTL);
}

static void sti_vid_init(struct sti_vid *vid)
{
	/* Enable PSI, Mask layer */
	writel(VID_CTL_PSI_ENABLE | VID_CTL_IGNORE, vid->regs + VID_CTL);

	/* Opaque */
	writel(VID_ALP_OPAQUE, vid->regs + VID_ALP);

	/* Brightness, contrast, tint, saturation */
	writel(VID_BC_DFLT, vid->regs + VID_BC);
	writel(VID_TINT_DFLT, vid->regs + VID_TINT);
	writel(VID_CSAT_DFLT, vid->regs + VID_CSAT);
}

struct sti_vid *sti_vid_create(struct device *dev, struct drm_device *drm_dev,
			       int id, void __iomem *baseaddr)
{
	struct sti_vid *vid;

	vid = devm_kzalloc(dev, sizeof(*vid), GFP_KERNEL);
	if (!vid) {
		DRM_ERROR("Failed to allocate memory for VID\n");
		return NULL;
	}

	vid->dev = dev;
	vid->regs = baseaddr;
	vid->id = id;

	sti_vid_init(vid);

	return vid;
}
