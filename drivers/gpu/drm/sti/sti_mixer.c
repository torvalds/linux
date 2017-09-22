/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <linux/seq_file.h>

#include "sti_compositor.h"
#include "sti_mixer.h"
#include "sti_vtg.h"

/* Module parameter to set the background color of the mixer */
static unsigned int bkg_color = 0x000000;
MODULE_PARM_DESC(bkgcolor, "Value of the background color 0xRRGGBB");
module_param_named(bkgcolor, bkg_color, int, 0644);

/* regs offset */
#define GAM_MIXER_CTL      0x00
#define GAM_MIXER_BKC      0x04
#define GAM_MIXER_BCO      0x0C
#define GAM_MIXER_BCS      0x10
#define GAM_MIXER_AVO      0x28
#define GAM_MIXER_AVS      0x2C
#define GAM_MIXER_CRB      0x34
#define GAM_MIXER_ACT      0x38
#define GAM_MIXER_MBP      0x3C
#define GAM_MIXER_MX0      0x80

/* id for depth of CRB reg */
#define GAM_DEPTH_VID0_ID  1
#define GAM_DEPTH_VID1_ID  2
#define GAM_DEPTH_GDP0_ID  3
#define GAM_DEPTH_GDP1_ID  4
#define GAM_DEPTH_GDP2_ID  5
#define GAM_DEPTH_GDP3_ID  6
#define GAM_DEPTH_MASK_ID  7

/* mask in CTL reg */
#define GAM_CTL_BACK_MASK  BIT(0)
#define GAM_CTL_VID0_MASK  BIT(1)
#define GAM_CTL_VID1_MASK  BIT(2)
#define GAM_CTL_GDP0_MASK  BIT(3)
#define GAM_CTL_GDP1_MASK  BIT(4)
#define GAM_CTL_GDP2_MASK  BIT(5)
#define GAM_CTL_GDP3_MASK  BIT(6)
#define GAM_CTL_CURSOR_MASK BIT(9)

const char *sti_mixer_to_str(struct sti_mixer *mixer)
{
	switch (mixer->id) {
	case STI_MIXER_MAIN:
		return "MAIN_MIXER";
	case STI_MIXER_AUX:
		return "AUX_MIXER";
	default:
		return "<UNKNOWN MIXER>";
	}
}

static inline u32 sti_mixer_reg_read(struct sti_mixer *mixer, u32 reg_id)
{
	return readl(mixer->regs + reg_id);
}

static inline void sti_mixer_reg_write(struct sti_mixer *mixer,
				       u32 reg_id, u32 val)
{
	writel(val, mixer->regs + reg_id);
}

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   sti_mixer_reg_read(mixer, reg))

static void mixer_dbg_ctl(struct seq_file *s, int val)
{
	unsigned int i;
	int count = 0;
	char *const disp_layer[] = {"BKG", "VID0", "VID1", "GDP0",
				    "GDP1", "GDP2", "GDP3"};

	seq_puts(s, "\tEnabled: ");
	for (i = 0; i < 7; i++) {
		if (val & 1) {
			seq_printf(s, "%s ", disp_layer[i]);
			count++;
		}
		val = val >> 1;
	}

	val = val >> 2;
	if (val & 1) {
		seq_puts(s, "CURS ");
		count++;
	}
	if (!count)
		seq_puts(s, "Nothing");
}

static void mixer_dbg_crb(struct seq_file *s, int val)
{
	int i;

	seq_puts(s, "\tDepth: ");
	for (i = 0; i < GAM_MIXER_NB_DEPTH_LEVEL; i++) {
		switch (val & GAM_DEPTH_MASK_ID) {
		case GAM_DEPTH_VID0_ID:
			seq_puts(s, "VID0");
			break;
		case GAM_DEPTH_VID1_ID:
			seq_puts(s, "VID1");
			break;
		case GAM_DEPTH_GDP0_ID:
			seq_puts(s, "GDP0");
			break;
		case GAM_DEPTH_GDP1_ID:
			seq_puts(s, "GDP1");
			break;
		case GAM_DEPTH_GDP2_ID:
			seq_puts(s, "GDP2");
			break;
		case GAM_DEPTH_GDP3_ID:
			seq_puts(s, "GDP3");
			break;
		default:
			seq_puts(s, "---");
		}

		if (i < GAM_MIXER_NB_DEPTH_LEVEL - 1)
			seq_puts(s, " < ");
		val = val >> 3;
	}
}

static void mixer_dbg_mxn(struct seq_file *s, void *addr)
{
	int i;

	for (i = 1; i < 8; i++)
		seq_printf(s, "-0x%08X", (int)readl(addr + i * 4));
}

static int mixer_dbg_show(struct seq_file *s, void *arg)
{
	struct drm_info_node *node = s->private;
	struct sti_mixer *mixer = (struct sti_mixer *)node->info_ent->data;

	seq_printf(s, "%s: (vaddr = 0x%p)",
		   sti_mixer_to_str(mixer), mixer->regs);

	DBGFS_DUMP(GAM_MIXER_CTL);
	mixer_dbg_ctl(s, sti_mixer_reg_read(mixer, GAM_MIXER_CTL));
	DBGFS_DUMP(GAM_MIXER_BKC);
	DBGFS_DUMP(GAM_MIXER_BCO);
	DBGFS_DUMP(GAM_MIXER_BCS);
	DBGFS_DUMP(GAM_MIXER_AVO);
	DBGFS_DUMP(GAM_MIXER_AVS);
	DBGFS_DUMP(GAM_MIXER_CRB);
	mixer_dbg_crb(s, sti_mixer_reg_read(mixer, GAM_MIXER_CRB));
	DBGFS_DUMP(GAM_MIXER_ACT);
	DBGFS_DUMP(GAM_MIXER_MBP);
	DBGFS_DUMP(GAM_MIXER_MX0);
	mixer_dbg_mxn(s, mixer->regs + GAM_MIXER_MX0);
	seq_putc(s, '\n');
	return 0;
}

static struct drm_info_list mixer0_debugfs_files[] = {
	{ "mixer_main", mixer_dbg_show, 0, NULL },
};

static struct drm_info_list mixer1_debugfs_files[] = {
	{ "mixer_aux", mixer_dbg_show, 0, NULL },
};

int sti_mixer_debugfs_init(struct sti_mixer *mixer, struct drm_minor *minor)
{
	unsigned int i;
	struct drm_info_list *mixer_debugfs_files;
	int nb_files;

	switch (mixer->id) {
	case STI_MIXER_MAIN:
		mixer_debugfs_files = mixer0_debugfs_files;
		nb_files = ARRAY_SIZE(mixer0_debugfs_files);
		break;
	case STI_MIXER_AUX:
		mixer_debugfs_files = mixer1_debugfs_files;
		nb_files = ARRAY_SIZE(mixer1_debugfs_files);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < nb_files; i++)
		mixer_debugfs_files[i].data = mixer;

	return drm_debugfs_create_files(mixer_debugfs_files,
					nb_files,
					minor->debugfs_root, minor);
}

void sti_mixer_set_background_status(struct sti_mixer *mixer, bool enable)
{
	u32 val = sti_mixer_reg_read(mixer, GAM_MIXER_CTL);

	val &= ~GAM_CTL_BACK_MASK;
	val |= enable;
	sti_mixer_reg_write(mixer, GAM_MIXER_CTL, val);
}

static void sti_mixer_set_background_color(struct sti_mixer *mixer,
					   unsigned int rgb)
{
	sti_mixer_reg_write(mixer, GAM_MIXER_BKC, rgb);
}

static void sti_mixer_set_background_area(struct sti_mixer *mixer,
					  struct drm_display_mode *mode)
{
	u32 ydo, xdo, yds, xds;

	ydo = sti_vtg_get_line_number(*mode, 0);
	yds = sti_vtg_get_line_number(*mode, mode->vdisplay - 1);
	xdo = sti_vtg_get_pixel_number(*mode, 0);
	xds = sti_vtg_get_pixel_number(*mode, mode->hdisplay - 1);

	sti_mixer_reg_write(mixer, GAM_MIXER_BCO, ydo << 16 | xdo);
	sti_mixer_reg_write(mixer, GAM_MIXER_BCS, yds << 16 | xds);
}

int sti_mixer_set_plane_depth(struct sti_mixer *mixer, struct sti_plane *plane)
{
	int plane_id, depth = plane->drm_plane.state->normalized_zpos;
	unsigned int i;
	u32 mask, val;

	switch (plane->desc) {
	case STI_GDP_0:
		plane_id = GAM_DEPTH_GDP0_ID;
		break;
	case STI_GDP_1:
		plane_id = GAM_DEPTH_GDP1_ID;
		break;
	case STI_GDP_2:
		plane_id = GAM_DEPTH_GDP2_ID;
		break;
	case STI_GDP_3:
		plane_id = GAM_DEPTH_GDP3_ID;
		break;
	case STI_HQVDP_0:
		plane_id = GAM_DEPTH_VID0_ID;
		break;
	case STI_CURSOR:
		/* no need to set depth for cursor */
		return 0;
	default:
		DRM_ERROR("Unknown plane %d\n", plane->desc);
		return 1;
	}

	/* Search if a previous depth was already assigned to the plane */
	val = sti_mixer_reg_read(mixer, GAM_MIXER_CRB);
	for (i = 0; i < GAM_MIXER_NB_DEPTH_LEVEL; i++) {
		mask = GAM_DEPTH_MASK_ID << (3 * i);
		if ((val & mask) == plane_id << (3 * i))
			break;
	}

	mask |= GAM_DEPTH_MASK_ID << (3 * depth);
	plane_id = plane_id << (3 * depth);

	DRM_DEBUG_DRIVER("%s %s depth=%d\n", sti_mixer_to_str(mixer),
			 sti_plane_to_str(plane), depth);
	dev_dbg(mixer->dev, "GAM_MIXER_CRB val 0x%x mask 0x%x\n",
		plane_id, mask);

	val &= ~mask;
	val |= plane_id;
	sti_mixer_reg_write(mixer, GAM_MIXER_CRB, val);

	dev_dbg(mixer->dev, "Read GAM_MIXER_CRB 0x%x\n",
		sti_mixer_reg_read(mixer, GAM_MIXER_CRB));
	return 0;
}

int sti_mixer_active_video_area(struct sti_mixer *mixer,
				struct drm_display_mode *mode)
{
	u32 ydo, xdo, yds, xds;

	ydo = sti_vtg_get_line_number(*mode, 0);
	yds = sti_vtg_get_line_number(*mode, mode->vdisplay - 1);
	xdo = sti_vtg_get_pixel_number(*mode, 0);
	xds = sti_vtg_get_pixel_number(*mode, mode->hdisplay - 1);

	DRM_DEBUG_DRIVER("%s active video area xdo:%d ydo:%d xds:%d yds:%d\n",
			 sti_mixer_to_str(mixer), xdo, ydo, xds, yds);
	sti_mixer_reg_write(mixer, GAM_MIXER_AVO, ydo << 16 | xdo);
	sti_mixer_reg_write(mixer, GAM_MIXER_AVS, yds << 16 | xds);

	sti_mixer_set_background_color(mixer, bkg_color);

	sti_mixer_set_background_area(mixer, mode);
	sti_mixer_set_background_status(mixer, true);
	return 0;
}

static u32 sti_mixer_get_plane_mask(struct sti_plane *plane)
{
	switch (plane->desc) {
	case STI_BACK:
		return GAM_CTL_BACK_MASK;
	case STI_GDP_0:
		return GAM_CTL_GDP0_MASK;
	case STI_GDP_1:
		return GAM_CTL_GDP1_MASK;
	case STI_GDP_2:
		return GAM_CTL_GDP2_MASK;
	case STI_GDP_3:
		return GAM_CTL_GDP3_MASK;
	case STI_HQVDP_0:
		return GAM_CTL_VID0_MASK;
	case STI_CURSOR:
		return GAM_CTL_CURSOR_MASK;
	default:
		return 0;
	}
}

int sti_mixer_set_plane_status(struct sti_mixer *mixer,
			       struct sti_plane *plane, bool status)
{
	u32 mask, val;

	DRM_DEBUG_DRIVER("%s %s %s\n", status ? "enable" : "disable",
			 sti_mixer_to_str(mixer), sti_plane_to_str(plane));

	mask = sti_mixer_get_plane_mask(plane);
	if (!mask) {
		DRM_ERROR("Can't find layer mask\n");
		return -EINVAL;
	}

	val = sti_mixer_reg_read(mixer, GAM_MIXER_CTL);
	val &= ~mask;
	val |= status ? mask : 0;
	sti_mixer_reg_write(mixer, GAM_MIXER_CTL, val);

	return 0;
}

struct sti_mixer *sti_mixer_create(struct device *dev,
				   struct drm_device *drm_dev,
				   int id,
				   void __iomem *baseaddr)
{
	struct sti_mixer *mixer = devm_kzalloc(dev, sizeof(*mixer), GFP_KERNEL);

	dev_dbg(dev, "%s\n", __func__);
	if (!mixer) {
		DRM_ERROR("Failed to allocated memory for mixer\n");
		return NULL;
	}
	mixer->regs = baseaddr;
	mixer->dev = dev;
	mixer->id = id;

	DRM_DEBUG_DRIVER("%s created. Regs=%p\n",
			 sti_mixer_to_str(mixer), mixer->regs);

	return mixer;
}
