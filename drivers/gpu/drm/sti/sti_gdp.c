/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "sti_compositor.h"
#include "sti_gdp.h"
#include "sti_plane.h"
#include "sti_vtg.h"

#define ALPHASWITCH     BIT(6)
#define ENA_COLOR_FILL  BIT(8)
#define BIGNOTLITTLE    BIT(23)
#define WAIT_NEXT_VSYNC BIT(31)

/* GDP color formats */
#define GDP_RGB565      0x00
#define GDP_RGB888      0x01
#define GDP_RGB888_32   0x02
#define GDP_XBGR8888    (GDP_RGB888_32 | BIGNOTLITTLE | ALPHASWITCH)
#define GDP_ARGB8565    0x04
#define GDP_ARGB8888    0x05
#define GDP_ABGR8888    (GDP_ARGB8888 | BIGNOTLITTLE | ALPHASWITCH)
#define GDP_ARGB1555    0x06
#define GDP_ARGB4444    0x07

#define GDP2STR(fmt) { GDP_ ## fmt, #fmt }

static struct gdp_format_to_str {
	int format;
	char name[20];
} gdp_format_to_str[] = {
		GDP2STR(RGB565),
		GDP2STR(RGB888),
		GDP2STR(RGB888_32),
		GDP2STR(XBGR8888),
		GDP2STR(ARGB8565),
		GDP2STR(ARGB8888),
		GDP2STR(ABGR8888),
		GDP2STR(ARGB1555),
		GDP2STR(ARGB4444)
		};

#define GAM_GDP_CTL_OFFSET      0x00
#define GAM_GDP_AGC_OFFSET      0x04
#define GAM_GDP_VPO_OFFSET      0x0C
#define GAM_GDP_VPS_OFFSET      0x10
#define GAM_GDP_PML_OFFSET      0x14
#define GAM_GDP_PMP_OFFSET      0x18
#define GAM_GDP_SIZE_OFFSET     0x1C
#define GAM_GDP_NVN_OFFSET      0x24
#define GAM_GDP_KEY1_OFFSET     0x28
#define GAM_GDP_KEY2_OFFSET     0x2C
#define GAM_GDP_PPT_OFFSET      0x34
#define GAM_GDP_CML_OFFSET      0x3C
#define GAM_GDP_MST_OFFSET      0x68

#define GAM_GDP_ALPHARANGE_255  BIT(5)
#define GAM_GDP_AGC_FULL_RANGE  0x00808080
#define GAM_GDP_PPT_IGNORE      (BIT(1) | BIT(0))
#define GAM_GDP_SIZE_MAX        0x7FF

#define GDP_NODE_NB_BANK        2
#define GDP_NODE_PER_FIELD      2

struct sti_gdp_node {
	u32 gam_gdp_ctl;
	u32 gam_gdp_agc;
	u32 reserved1;
	u32 gam_gdp_vpo;
	u32 gam_gdp_vps;
	u32 gam_gdp_pml;
	u32 gam_gdp_pmp;
	u32 gam_gdp_size;
	u32 reserved2;
	u32 gam_gdp_nvn;
	u32 gam_gdp_key1;
	u32 gam_gdp_key2;
	u32 reserved3;
	u32 gam_gdp_ppt;
	u32 reserved4;
	u32 gam_gdp_cml;
};

struct sti_gdp_node_list {
	struct sti_gdp_node *top_field;
	dma_addr_t top_field_paddr;
	struct sti_gdp_node *btm_field;
	dma_addr_t btm_field_paddr;
};

/**
 * STI GDP structure
 *
 * @sti_plane:          sti_plane structure
 * @dev:                driver device
 * @regs:               gdp registers
 * @clk_pix:            pixel clock for the current gdp
 * @clk_main_parent:    gdp parent clock if main path used
 * @clk_aux_parent:     gdp parent clock if aux path used
 * @vtg_field_nb:       callback for VTG FIELD (top or bottom) notification
 * @is_curr_top:        true if the current node processed is the top field
 * @node_list:          array of node list
 * @vtg:                registered vtg
 */
struct sti_gdp {
	struct sti_plane plane;
	struct device *dev;
	void __iomem *regs;
	struct clk *clk_pix;
	struct clk *clk_main_parent;
	struct clk *clk_aux_parent;
	struct notifier_block vtg_field_nb;
	bool is_curr_top;
	struct sti_gdp_node_list node_list[GDP_NODE_NB_BANK];
	struct sti_vtg *vtg;
};

#define to_sti_gdp(x) container_of(x, struct sti_gdp, plane)

static const uint32_t gdp_supported_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
};

#define DBGFS_DUMP(reg) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
				   readl(gdp->regs + reg ## _OFFSET))

static void gdp_dbg_ctl(struct seq_file *s, int val)
{
	int i;

	seq_puts(s, "\tColor:");
	for (i = 0; i < ARRAY_SIZE(gdp_format_to_str); i++) {
		if (gdp_format_to_str[i].format == (val & 0x1F)) {
			seq_printf(s, gdp_format_to_str[i].name);
			break;
		}
	}
	if (i == ARRAY_SIZE(gdp_format_to_str))
		seq_puts(s, "<UNKNOWN>");

	seq_printf(s, "\tWaitNextVsync:%d", val & WAIT_NEXT_VSYNC ? 1 : 0);
}

static void gdp_dbg_vpo(struct seq_file *s, int val)
{
	seq_printf(s, "\txdo:%4d\tydo:%4d", val & 0xFFFF, (val >> 16) & 0xFFFF);
}

static void gdp_dbg_vps(struct seq_file *s, int val)
{
	seq_printf(s, "\txds:%4d\tyds:%4d", val & 0xFFFF, (val >> 16) & 0xFFFF);
}

static void gdp_dbg_size(struct seq_file *s, int val)
{
	seq_printf(s, "\t%d x %d", val & 0xFFFF, (val >> 16) & 0xFFFF);
}

static void gdp_dbg_nvn(struct seq_file *s, struct sti_gdp *gdp, int val)
{
	void *base = NULL;
	unsigned int i;

	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		if (gdp->node_list[i].top_field_paddr == val) {
			base = gdp->node_list[i].top_field;
			break;
		}
		if (gdp->node_list[i].btm_field_paddr == val) {
			base = gdp->node_list[i].btm_field;
			break;
		}
	}

	if (base)
		seq_printf(s, "\tVirt @: %p", base);
}

static void gdp_dbg_ppt(struct seq_file *s, int val)
{
	if (val & GAM_GDP_PPT_IGNORE)
		seq_puts(s, "\tNot displayed on mixer!");
}

static void gdp_dbg_mst(struct seq_file *s, int val)
{
	if (val & 1)
		seq_puts(s, "\tBUFFER UNDERFLOW!");
}

static int gdp_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_gdp *gdp = (struct sti_gdp *)node->info_ent->data;
	struct drm_device *dev = node->minor->dev;
	struct drm_plane *drm_plane = &gdp->plane.drm_plane;
	struct drm_crtc *crtc = drm_plane->crtc;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(s, "%s: (vaddr = 0x%p)",
		   sti_plane_to_str(&gdp->plane), gdp->regs);

	DBGFS_DUMP(GAM_GDP_CTL);
	gdp_dbg_ctl(s, readl(gdp->regs + GAM_GDP_CTL_OFFSET));
	DBGFS_DUMP(GAM_GDP_AGC);
	DBGFS_DUMP(GAM_GDP_VPO);
	gdp_dbg_vpo(s, readl(gdp->regs + GAM_GDP_VPO_OFFSET));
	DBGFS_DUMP(GAM_GDP_VPS);
	gdp_dbg_vps(s, readl(gdp->regs + GAM_GDP_VPS_OFFSET));
	DBGFS_DUMP(GAM_GDP_PML);
	DBGFS_DUMP(GAM_GDP_PMP);
	DBGFS_DUMP(GAM_GDP_SIZE);
	gdp_dbg_size(s, readl(gdp->regs + GAM_GDP_SIZE_OFFSET));
	DBGFS_DUMP(GAM_GDP_NVN);
	gdp_dbg_nvn(s, gdp, readl(gdp->regs + GAM_GDP_NVN_OFFSET));
	DBGFS_DUMP(GAM_GDP_KEY1);
	DBGFS_DUMP(GAM_GDP_KEY2);
	DBGFS_DUMP(GAM_GDP_PPT);
	gdp_dbg_ppt(s, readl(gdp->regs + GAM_GDP_PPT_OFFSET));
	DBGFS_DUMP(GAM_GDP_CML);
	DBGFS_DUMP(GAM_GDP_MST);
	gdp_dbg_mst(s, readl(gdp->regs + GAM_GDP_MST_OFFSET));

	seq_puts(s, "\n\n");
	if (!crtc)
		seq_puts(s, "  Not connected to any DRM CRTC\n");
	else
		seq_printf(s, "  Connected to DRM CRTC #%d (%s)\n",
			   crtc->base.id, sti_mixer_to_str(to_sti_mixer(crtc)));

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

static void gdp_node_dump_node(struct seq_file *s, struct sti_gdp_node *node)
{
	seq_printf(s, "\t@:0x%p", node);
	seq_printf(s, "\n\tCTL  0x%08X", node->gam_gdp_ctl);
	gdp_dbg_ctl(s, node->gam_gdp_ctl);
	seq_printf(s, "\n\tAGC  0x%08X", node->gam_gdp_agc);
	seq_printf(s, "\n\tVPO  0x%08X", node->gam_gdp_vpo);
	gdp_dbg_vpo(s, node->gam_gdp_vpo);
	seq_printf(s, "\n\tVPS  0x%08X", node->gam_gdp_vps);
	gdp_dbg_vps(s, node->gam_gdp_vps);
	seq_printf(s, "\n\tPML  0x%08X", node->gam_gdp_pml);
	seq_printf(s, "\n\tPMP  0x%08X", node->gam_gdp_pmp);
	seq_printf(s, "\n\tSIZE 0x%08X", node->gam_gdp_size);
	gdp_dbg_size(s, node->gam_gdp_size);
	seq_printf(s, "\n\tNVN  0x%08X", node->gam_gdp_nvn);
	seq_printf(s, "\n\tKEY1 0x%08X", node->gam_gdp_key1);
	seq_printf(s, "\n\tKEY2 0x%08X", node->gam_gdp_key2);
	seq_printf(s, "\n\tPPT  0x%08X", node->gam_gdp_ppt);
	gdp_dbg_ppt(s, node->gam_gdp_ppt);
	seq_printf(s, "\n\tCML  0x%08X", node->gam_gdp_cml);
	seq_puts(s, "\n");
}

static int gdp_node_dbg_show(struct seq_file *s, void *arg)
{
	struct drm_info_node *node = s->private;
	struct sti_gdp *gdp = (struct sti_gdp *)node->info_ent->data;
	struct drm_device *dev = node->minor->dev;
	unsigned int b;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	for (b = 0; b < GDP_NODE_NB_BANK; b++) {
		seq_printf(s, "\n%s[%d].top", sti_plane_to_str(&gdp->plane), b);
		gdp_node_dump_node(s, gdp->node_list[b].top_field);
		seq_printf(s, "\n%s[%d].btm", sti_plane_to_str(&gdp->plane), b);
		gdp_node_dump_node(s, gdp->node_list[b].btm_field);
	}

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

static struct drm_info_list gdp0_debugfs_files[] = {
	{ "gdp0", gdp_dbg_show, 0, NULL },
	{ "gdp0_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp1_debugfs_files[] = {
	{ "gdp1", gdp_dbg_show, 0, NULL },
	{ "gdp1_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp2_debugfs_files[] = {
	{ "gdp2", gdp_dbg_show, 0, NULL },
	{ "gdp2_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp3_debugfs_files[] = {
	{ "gdp3", gdp_dbg_show, 0, NULL },
	{ "gdp3_node", gdp_node_dbg_show, 0, NULL },
};

static int gdp_debugfs_init(struct sti_gdp *gdp, struct drm_minor *minor)
{
	unsigned int i;
	struct drm_info_list *gdp_debugfs_files;
	int nb_files;

	switch (gdp->plane.desc) {
	case STI_GDP_0:
		gdp_debugfs_files = gdp0_debugfs_files;
		nb_files = ARRAY_SIZE(gdp0_debugfs_files);
		break;
	case STI_GDP_1:
		gdp_debugfs_files = gdp1_debugfs_files;
		nb_files = ARRAY_SIZE(gdp1_debugfs_files);
		break;
	case STI_GDP_2:
		gdp_debugfs_files = gdp2_debugfs_files;
		nb_files = ARRAY_SIZE(gdp2_debugfs_files);
		break;
	case STI_GDP_3:
		gdp_debugfs_files = gdp3_debugfs_files;
		nb_files = ARRAY_SIZE(gdp3_debugfs_files);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < nb_files; i++)
		gdp_debugfs_files[i].data = gdp;

	return drm_debugfs_create_files(gdp_debugfs_files,
					nb_files,
					minor->debugfs_root, minor);
}

static int sti_gdp_fourcc2format(int fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_XRGB8888:
		return GDP_RGB888_32;
	case DRM_FORMAT_XBGR8888:
		return GDP_XBGR8888;
	case DRM_FORMAT_ARGB8888:
		return GDP_ARGB8888;
	case DRM_FORMAT_ABGR8888:
		return GDP_ABGR8888;
	case DRM_FORMAT_ARGB4444:
		return GDP_ARGB4444;
	case DRM_FORMAT_ARGB1555:
		return GDP_ARGB1555;
	case DRM_FORMAT_RGB565:
		return GDP_RGB565;
	case DRM_FORMAT_RGB888:
		return GDP_RGB888;
	}
	return -1;
}

static int sti_gdp_get_alpharange(int format)
{
	switch (format) {
	case GDP_ARGB8565:
	case GDP_ARGB8888:
	case GDP_ABGR8888:
		return GAM_GDP_ALPHARANGE_255;
	}
	return 0;
}

/**
 * sti_gdp_get_free_nodes
 * @gdp: gdp pointer
 *
 * Look for a GDP node list that is not currently read by the HW.
 *
 * RETURNS:
 * Pointer to the free GDP node list
 */
static struct sti_gdp_node_list *sti_gdp_get_free_nodes(struct sti_gdp *gdp)
{
	int hw_nvn;
	unsigned int i;

	hw_nvn = readl(gdp->regs + GAM_GDP_NVN_OFFSET);
	if (!hw_nvn)
		goto end;

	for (i = 0; i < GDP_NODE_NB_BANK; i++)
		if ((hw_nvn != gdp->node_list[i].btm_field_paddr) &&
		    (hw_nvn != gdp->node_list[i].top_field_paddr))
			return &gdp->node_list[i];

	/* in hazardious cases restart with the first node */
	DRM_ERROR("inconsistent NVN for %s: 0x%08X\n",
			sti_plane_to_str(&gdp->plane), hw_nvn);

end:
	return &gdp->node_list[0];
}

/**
 * sti_gdp_get_current_nodes
 * @gdp: gdp pointer
 *
 * Look for GDP nodes that are currently read by the HW.
 *
 * RETURNS:
 * Pointer to the current GDP node list
 */
static
struct sti_gdp_node_list *sti_gdp_get_current_nodes(struct sti_gdp *gdp)
{
	int hw_nvn;
	unsigned int i;

	hw_nvn = readl(gdp->regs + GAM_GDP_NVN_OFFSET);
	if (!hw_nvn)
		goto end;

	for (i = 0; i < GDP_NODE_NB_BANK; i++)
		if ((hw_nvn == gdp->node_list[i].btm_field_paddr) ||
				(hw_nvn == gdp->node_list[i].top_field_paddr))
			return &gdp->node_list[i];

end:
	DRM_DEBUG_DRIVER("Warning, NVN 0x%08X for %s does not match any node\n",
				hw_nvn, sti_plane_to_str(&gdp->plane));

	return NULL;
}

/**
 * sti_gdp_disable
 * @gdp: gdp pointer
 *
 * Disable a GDP.
 */
static void sti_gdp_disable(struct sti_gdp *gdp)
{
	unsigned int i;

	DRM_DEBUG_DRIVER("%s\n", sti_plane_to_str(&gdp->plane));

	/* Set the nodes as 'to be ignored on mixer' */
	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		gdp->node_list[i].top_field->gam_gdp_ppt |= GAM_GDP_PPT_IGNORE;
		gdp->node_list[i].btm_field->gam_gdp_ppt |= GAM_GDP_PPT_IGNORE;
	}

	if (sti_vtg_unregister_client(gdp->vtg, &gdp->vtg_field_nb))
		DRM_DEBUG_DRIVER("Warning: cannot unregister VTG notifier\n");

	if (gdp->clk_pix)
		clk_disable_unprepare(gdp->clk_pix);

	gdp->plane.status = STI_PLANE_DISABLED;
}

/**
 * sti_gdp_field_cb
 * @nb: notifier block
 * @event: event message
 * @data: private data
 *
 * Handle VTG top field and bottom field event.
 *
 * RETURNS:
 * 0 on success.
 */
int sti_gdp_field_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct sti_gdp *gdp = container_of(nb, struct sti_gdp, vtg_field_nb);

	if (gdp->plane.status == STI_PLANE_FLUSHING) {
		/* disable need to be synchronize on vsync event */
		DRM_DEBUG_DRIVER("Vsync event received => disable %s\n",
				 sti_plane_to_str(&gdp->plane));

		sti_gdp_disable(gdp);
	}

	switch (event) {
	case VTG_TOP_FIELD_EVENT:
		gdp->is_curr_top = true;
		break;
	case VTG_BOTTOM_FIELD_EVENT:
		gdp->is_curr_top = false;
		break;
	default:
		DRM_ERROR("unsupported event: %lu\n", event);
		break;
	}

	return 0;
}

static void sti_gdp_init(struct sti_gdp *gdp)
{
	struct device_node *np = gdp->dev->of_node;
	dma_addr_t dma_addr;
	void *base;
	unsigned int i, size;

	/* Allocate all the nodes within a single memory page */
	size = sizeof(struct sti_gdp_node) *
	    GDP_NODE_PER_FIELD * GDP_NODE_NB_BANK;
	base = dma_alloc_wc(gdp->dev, size, &dma_addr, GFP_KERNEL | GFP_DMA);

	if (!base) {
		DRM_ERROR("Failed to allocate memory for GDP node\n");
		return;
	}
	memset(base, 0, size);

	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		if (dma_addr & 0xF) {
			DRM_ERROR("Mem alignment failed\n");
			return;
		}
		gdp->node_list[i].top_field = base;
		gdp->node_list[i].top_field_paddr = dma_addr;

		DRM_DEBUG_DRIVER("node[%d].top_field=%p\n", i, base);
		base += sizeof(struct sti_gdp_node);
		dma_addr += sizeof(struct sti_gdp_node);

		if (dma_addr & 0xF) {
			DRM_ERROR("Mem alignment failed\n");
			return;
		}
		gdp->node_list[i].btm_field = base;
		gdp->node_list[i].btm_field_paddr = dma_addr;
		DRM_DEBUG_DRIVER("node[%d].btm_field=%p\n", i, base);
		base += sizeof(struct sti_gdp_node);
		dma_addr += sizeof(struct sti_gdp_node);
	}

	if (of_device_is_compatible(np, "st,stih407-compositor")) {
		/* GDP of STiH407 chip have its own pixel clock */
		char *clk_name;

		switch (gdp->plane.desc) {
		case STI_GDP_0:
			clk_name = "pix_gdp1";
			break;
		case STI_GDP_1:
			clk_name = "pix_gdp2";
			break;
		case STI_GDP_2:
			clk_name = "pix_gdp3";
			break;
		case STI_GDP_3:
			clk_name = "pix_gdp4";
			break;
		default:
			DRM_ERROR("GDP id not recognized\n");
			return;
		}

		gdp->clk_pix = devm_clk_get(gdp->dev, clk_name);
		if (IS_ERR(gdp->clk_pix))
			DRM_ERROR("Cannot get %s clock\n", clk_name);

		gdp->clk_main_parent = devm_clk_get(gdp->dev, "main_parent");
		if (IS_ERR(gdp->clk_main_parent))
			DRM_ERROR("Cannot get main_parent clock\n");

		gdp->clk_aux_parent = devm_clk_get(gdp->dev, "aux_parent");
		if (IS_ERR(gdp->clk_aux_parent))
			DRM_ERROR("Cannot get aux_parent clock\n");
	}
}

/**
 * sti_gdp_get_dst
 * @dev: device
 * @dst: requested destination size
 * @src: source size
 *
 * Return the cropped / clamped destination size
 *
 * RETURNS:
 * cropped / clamped destination size
 */
static int sti_gdp_get_dst(struct device *dev, int dst, int src)
{
	if (dst == src)
		return dst;

	if (dst < src) {
		dev_dbg(dev, "WARNING: GDP scale not supported, will crop\n");
		return dst;
	}

	dev_dbg(dev, "WARNING: GDP scale not supported, will clamp\n");
	return src;
}

static int sti_gdp_atomic_check(struct drm_plane *drm_plane,
				struct drm_plane_state *state)
{
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_gdp *gdp = to_sti_gdp(plane);
	struct drm_crtc *crtc = state->crtc;
	struct sti_compositor *compo = dev_get_drvdata(gdp->dev);
	struct drm_framebuffer *fb =  state->fb;
	bool first_prepare = plane->status == STI_PLANE_DISABLED ? true : false;
	struct drm_crtc_state *crtc_state;
	struct sti_mixer *mixer;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;
	int format;

	/* no need for further checks if the plane is being disabled */
	if (!crtc || !fb)
		return 0;

	mixer = to_sti_mixer(crtc);
	crtc_state = drm_atomic_get_crtc_state(state->state, crtc);
	mode = &crtc_state->mode;
	dst_x = state->crtc_x;
	dst_y = state->crtc_y;
	dst_w = clamp_val(state->crtc_w, 0, mode->crtc_hdisplay - dst_x);
	dst_h = clamp_val(state->crtc_h, 0, mode->crtc_vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = clamp_val(state->src_w >> 16, 0, GAM_GDP_SIZE_MAX);
	src_h = clamp_val(state->src_h >> 16, 0, GAM_GDP_SIZE_MAX);

	format = sti_gdp_fourcc2format(fb->pixel_format);
	if (format == -1) {
		DRM_ERROR("Format not supported by GDP %.4s\n",
			  (char *)&fb->pixel_format);
		return -EINVAL;
	}

	if (!drm_fb_cma_get_gem_obj(fb, 0)) {
		DRM_ERROR("Can't get CMA GEM object for fb\n");
		return -EINVAL;
	}

	if (first_prepare) {
		/* Register gdp callback */
		gdp->vtg = mixer->id == STI_MIXER_MAIN ?
					compo->vtg_main : compo->vtg_aux;
		if (sti_vtg_register_client(gdp->vtg,
					    &gdp->vtg_field_nb, crtc)) {
			DRM_ERROR("Cannot register VTG notifier\n");
			return -EINVAL;
		}

		/* Set and enable gdp clock */
		if (gdp->clk_pix) {
			struct clk *clkp;
			int rate = mode->clock * 1000;
			int res;

			/*
			 * According to the mixer used, the gdp pixel clock
			 * should have a different parent clock.
			 */
			if (mixer->id == STI_MIXER_MAIN)
				clkp = gdp->clk_main_parent;
			else
				clkp = gdp->clk_aux_parent;

			if (clkp)
				clk_set_parent(gdp->clk_pix, clkp);

			res = clk_set_rate(gdp->clk_pix, rate);
			if (res < 0) {
				DRM_ERROR("Cannot set rate (%dHz) for gdp\n",
					  rate);
				return -EINVAL;
			}

			if (clk_prepare_enable(gdp->clk_pix)) {
				DRM_ERROR("Failed to prepare/enable gdp\n");
				return -EINVAL;
			}
		}
	}

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(mixer),
		      drm_plane->base.id, sti_plane_to_str(plane));
	DRM_DEBUG_KMS("%s dst=(%dx%d)@(%d,%d) - src=(%dx%d)@(%d,%d)\n",
		      sti_plane_to_str(plane),
		      dst_w, dst_h, dst_x, dst_y,
		      src_w, src_h, src_x, src_y);

	return 0;
}

static void sti_gdp_atomic_update(struct drm_plane *drm_plane,
				  struct drm_plane_state *oldstate)
{
	struct drm_plane_state *state = drm_plane->state;
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_gdp *gdp = to_sti_gdp(plane);
	struct drm_crtc *crtc = state->crtc;
	struct drm_framebuffer *fb =  state->fb;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;
	struct drm_gem_cma_object *cma_obj;
	struct sti_gdp_node_list *list;
	struct sti_gdp_node_list *curr_list;
	struct sti_gdp_node *top_field, *btm_field;
	u32 dma_updated_top;
	u32 dma_updated_btm;
	int format;
	unsigned int depth, bpp;
	u32 ydo, xdo, yds, xds;

	if (!crtc || !fb)
		return;

	mode = &crtc->mode;
	dst_x = state->crtc_x;
	dst_y = state->crtc_y;
	dst_w = clamp_val(state->crtc_w, 0, mode->crtc_hdisplay - dst_x);
	dst_h = clamp_val(state->crtc_h, 0, mode->crtc_vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = clamp_val(state->src_w >> 16, 0, GAM_GDP_SIZE_MAX);
	src_h = clamp_val(state->src_h >> 16, 0, GAM_GDP_SIZE_MAX);

	list = sti_gdp_get_free_nodes(gdp);
	top_field = list->top_field;
	btm_field = list->btm_field;

	dev_dbg(gdp->dev, "%s %s top_node:0x%p btm_node:0x%p\n", __func__,
		sti_plane_to_str(plane), top_field, btm_field);

	/* build the top field */
	top_field->gam_gdp_agc = GAM_GDP_AGC_FULL_RANGE;
	top_field->gam_gdp_ctl = WAIT_NEXT_VSYNC;
	format = sti_gdp_fourcc2format(fb->pixel_format);
	top_field->gam_gdp_ctl |= format;
	top_field->gam_gdp_ctl |= sti_gdp_get_alpharange(format);
	top_field->gam_gdp_ppt &= ~GAM_GDP_PPT_IGNORE;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);

	DRM_DEBUG_DRIVER("drm FB:%d format:%.4s phys@:0x%lx\n", fb->base.id,
			 (char *)&fb->pixel_format,
			 (unsigned long)cma_obj->paddr);

	/* pixel memory location */
	drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
	top_field->gam_gdp_pml = (u32)cma_obj->paddr + fb->offsets[0];
	top_field->gam_gdp_pml += src_x * (bpp >> 3);
	top_field->gam_gdp_pml += src_y * fb->pitches[0];

	/* output parameters (clamped / cropped) */
	dst_w = sti_gdp_get_dst(gdp->dev, dst_w, src_w);
	dst_h = sti_gdp_get_dst(gdp->dev, dst_h, src_h);
	ydo = sti_vtg_get_line_number(*mode, dst_y);
	yds = sti_vtg_get_line_number(*mode, dst_y + dst_h - 1);
	xdo = sti_vtg_get_pixel_number(*mode, dst_x);
	xds = sti_vtg_get_pixel_number(*mode, dst_x + dst_w - 1);
	top_field->gam_gdp_vpo = (ydo << 16) | xdo;
	top_field->gam_gdp_vps = (yds << 16) | xds;

	/* input parameters */
	src_w = dst_w;
	top_field->gam_gdp_pmp = fb->pitches[0];
	top_field->gam_gdp_size = src_h << 16 | src_w;

	/* Same content and chained together */
	memcpy(btm_field, top_field, sizeof(*btm_field));
	top_field->gam_gdp_nvn = list->btm_field_paddr;
	btm_field->gam_gdp_nvn = list->top_field_paddr;

	/* Interlaced mode */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		btm_field->gam_gdp_pml = top_field->gam_gdp_pml +
					 fb->pitches[0];

	/* Update the NVN field of the 'right' field of the current GDP node
	 * (being used by the HW) with the address of the updated ('free') top
	 * field GDP node.
	 * - In interlaced mode the 'right' field is the bottom field as we
	 *   update frames starting from their top field
	 * - In progressive mode, we update both bottom and top fields which
	 *   are equal nodes.
	 * At the next VSYNC, the updated node list will be used by the HW.
	 */
	curr_list = sti_gdp_get_current_nodes(gdp);
	dma_updated_top = list->top_field_paddr;
	dma_updated_btm = list->btm_field_paddr;

	dev_dbg(gdp->dev, "Current NVN:0x%X\n",
		readl(gdp->regs + GAM_GDP_NVN_OFFSET));
	dev_dbg(gdp->dev, "Posted buff: %lx current buff: %x\n",
		(unsigned long)cma_obj->paddr,
		readl(gdp->regs + GAM_GDP_PML_OFFSET));

	if (!curr_list) {
		/* First update or invalid node should directly write in the
		 * hw register */
		DRM_DEBUG_DRIVER("%s first update (or invalid node)",
				 sti_plane_to_str(plane));

		writel(gdp->is_curr_top ?
				dma_updated_btm : dma_updated_top,
				gdp->regs + GAM_GDP_NVN_OFFSET);
		goto end;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		if (gdp->is_curr_top) {
			/* Do not update in the middle of the frame, but
			 * postpone the update after the bottom field has
			 * been displayed */
			curr_list->btm_field->gam_gdp_nvn = dma_updated_top;
		} else {
			/* Direct update to avoid one frame delay */
			writel(dma_updated_top,
			       gdp->regs + GAM_GDP_NVN_OFFSET);
		}
	} else {
		/* Direct update for progressive to avoid one frame delay */
		writel(dma_updated_top, gdp->regs + GAM_GDP_NVN_OFFSET);
	}

end:
	sti_plane_update_fps(plane, true, false);

	plane->status = STI_PLANE_UPDATED;
}

static void sti_gdp_atomic_disable(struct drm_plane *drm_plane,
				   struct drm_plane_state *oldstate)
{
	struct sti_plane *plane = to_sti_plane(drm_plane);

	if (!drm_plane->crtc) {
		DRM_DEBUG_DRIVER("drm plane:%d not enabled\n",
				 drm_plane->base.id);
		return;
	}

	DRM_DEBUG_DRIVER("CRTC:%d (%s) drm plane:%d (%s)\n",
			 drm_plane->crtc->base.id,
			 sti_mixer_to_str(to_sti_mixer(drm_plane->crtc)),
			 drm_plane->base.id, sti_plane_to_str(plane));

	plane->status = STI_PLANE_DISABLING;
}

static const struct drm_plane_helper_funcs sti_gdp_helpers_funcs = {
	.atomic_check = sti_gdp_atomic_check,
	.atomic_update = sti_gdp_atomic_update,
	.atomic_disable = sti_gdp_atomic_disable,
};

struct drm_plane *sti_gdp_create(struct drm_device *drm_dev,
				 struct device *dev, int desc,
				 void __iomem *baseaddr,
				 unsigned int possible_crtcs,
				 enum drm_plane_type type)
{
	struct sti_gdp *gdp;
	int res;

	gdp = devm_kzalloc(dev, sizeof(*gdp), GFP_KERNEL);
	if (!gdp) {
		DRM_ERROR("Failed to allocate memory for GDP\n");
		return NULL;
	}

	gdp->dev = dev;
	gdp->regs = baseaddr;
	gdp->plane.desc = desc;
	gdp->plane.status = STI_PLANE_DISABLED;

	gdp->vtg_field_nb.notifier_call = sti_gdp_field_cb;

	sti_gdp_init(gdp);

	res = drm_universal_plane_init(drm_dev, &gdp->plane.drm_plane,
				       possible_crtcs,
				       &sti_plane_helpers_funcs,
				       gdp_supported_formats,
				       ARRAY_SIZE(gdp_supported_formats),
				       type, NULL);
	if (res) {
		DRM_ERROR("Failed to initialize universal plane\n");
		goto err;
	}

	drm_plane_helper_add(&gdp->plane.drm_plane, &sti_gdp_helpers_funcs);

	sti_plane_init_property(&gdp->plane, type);

	if (gdp_debugfs_init(gdp, drm_dev->primary))
		DRM_ERROR("GDP debugfs setup failed\n");

	return &gdp->plane.drm_plane;

err:
	devm_kfree(dev, gdp);
	return NULL;
}
