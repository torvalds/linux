// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bug.h>
#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_client_setup.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_module.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>

#include <video/vga.h>

/* ---------------------------------------------------------------------- */

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

static int bochs_modeset = -1;
static int defx = 1024;
static int defy = 768;

module_param_named(modeset, bochs_modeset, int, 0444);
MODULE_PARM_DESC(modeset, "enable/disable kernel modesetting");

module_param(defx, int, 0444);
module_param(defy, int, 0444);
MODULE_PARM_DESC(defx, "default x resolution");
MODULE_PARM_DESC(defy, "default y resolution");

/* ---------------------------------------------------------------------- */

enum bochs_types {
	BOCHS_QEMU_STDVGA,
	BOCHS_SIMICS,
	BOCHS_UNKNOWN,
};

struct bochs_device {
	struct drm_device dev;

	/* hw */
	void __iomem   *mmio;
	int            ioports;
	void __iomem   *fb_map;
	unsigned long  fb_base;
	unsigned long  fb_size;
	unsigned long  qext_size;

	/* mode */
	u16 xres;
	u16 yres;
	u16 yres_virtual;
	u32 stride;
	u32 bpp;

	/* drm */
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct bochs_device *to_bochs_device(const struct drm_device *dev)
{
	return container_of(dev, struct bochs_device, dev);
}

/* ---------------------------------------------------------------------- */

static __always_inline bool bochs_uses_mmio(struct bochs_device *bochs)
{
	return !IS_ENABLED(CONFIG_HAS_IOPORT) || bochs->mmio;
}

static void bochs_vga_writeb(struct bochs_device *bochs, u16 ioport, u8 val)
{
	if (WARN_ON(ioport < 0x3c0 || ioport > 0x3df))
		return;

	if (bochs_uses_mmio(bochs)) {
		int offset = ioport - 0x3c0 + 0x400;

		writeb(val, bochs->mmio + offset);
	} else {
		outb(val, ioport);
	}
}

static u8 bochs_vga_readb(struct bochs_device *bochs, u16 ioport)
{
	if (WARN_ON(ioport < 0x3c0 || ioport > 0x3df))
		return 0xff;

	if (bochs_uses_mmio(bochs)) {
		int offset = ioport - 0x3c0 + 0x400;

		return readb(bochs->mmio + offset);
	} else {
		return inb(ioport);
	}
}

static u16 bochs_dispi_read(struct bochs_device *bochs, u16 reg)
{
	u16 ret = 0;

	if (bochs_uses_mmio(bochs)) {
		int offset = 0x500 + (reg << 1);

		ret = readw(bochs->mmio + offset);
	} else {
		outw(reg, VBE_DISPI_IOPORT_INDEX);
		ret = inw(VBE_DISPI_IOPORT_DATA);
	}
	return ret;
}

static void bochs_dispi_write(struct bochs_device *bochs, u16 reg, u16 val)
{
	if (bochs_uses_mmio(bochs)) {
		int offset = 0x500 + (reg << 1);

		writew(val, bochs->mmio + offset);
	} else {
		outw(reg, VBE_DISPI_IOPORT_INDEX);
		outw(val, VBE_DISPI_IOPORT_DATA);
	}
}

static void bochs_hw_set_big_endian(struct bochs_device *bochs)
{
	if (bochs->qext_size < 8)
		return;

	writel(0xbebebebe, bochs->mmio + 0x604);
}

static void bochs_hw_set_little_endian(struct bochs_device *bochs)
{
	if (bochs->qext_size < 8)
		return;

	writel(0x1e1e1e1e, bochs->mmio + 0x604);
}

#ifdef __BIG_ENDIAN
#define bochs_hw_set_native_endian(_b) bochs_hw_set_big_endian(_b)
#else
#define bochs_hw_set_native_endian(_b) bochs_hw_set_little_endian(_b)
#endif

static int bochs_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct bochs_device *bochs = data;
	size_t i, start = block * EDID_LENGTH;

	if (!bochs->mmio)
		return -1;

	if (start + len > 0x400 /* vga register offset */)
		return -1;

	for (i = 0; i < len; i++)
		buf[i] = readb(bochs->mmio + start + i);

	return 0;
}

static const struct drm_edid *bochs_hw_read_edid(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct bochs_device *bochs = to_bochs_device(dev);
	u8 header[8];

	/* check header to detect whenever edid support is enabled in qemu */
	bochs_get_edid_block(bochs, header, 0, ARRAY_SIZE(header));
	if (drm_edid_header_is_valid(header) != 8)
		return NULL;

	drm_dbg(dev, "Found EDID data blob.\n");

	return drm_edid_read_custom(connector, bochs_get_edid_block, bochs);
}

static int bochs_hw_init(struct bochs_device *bochs)
{
	struct drm_device *dev = &bochs->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned long addr, size, mem, ioaddr, iosize;
	u16 id;

	if (pdev->resource[2].flags & IORESOURCE_MEM) {
		ioaddr = pci_resource_start(pdev, 2);
		iosize = pci_resource_len(pdev, 2);
		/* mmio bar with vga and bochs registers present */
		if (!devm_request_mem_region(&pdev->dev, ioaddr, iosize, "bochs-drm")) {
			DRM_ERROR("Cannot request mmio region\n");
			return -EBUSY;
		}
		bochs->mmio = devm_ioremap(&pdev->dev, ioaddr, iosize);
		if (bochs->mmio == NULL) {
			DRM_ERROR("Cannot map mmio region\n");
			return -ENOMEM;
		}
	} else if (IS_ENABLED(CONFIG_HAS_IOPORT)) {
		ioaddr = VBE_DISPI_IOPORT_INDEX;
		iosize = 2;
		if (!devm_request_region(&pdev->dev, ioaddr, iosize, "bochs-drm")) {
			DRM_ERROR("Cannot request ioports\n");
			return -EBUSY;
		}
		bochs->ioports = 1;
	} else {
		dev_err(dev->dev, "I/O ports are not supported\n");
		return -EIO;
	}

	id = bochs_dispi_read(bochs, VBE_DISPI_INDEX_ID);
	mem = bochs_dispi_read(bochs, VBE_DISPI_INDEX_VIDEO_MEMORY_64K)
		* 64 * 1024;
	if ((id & 0xfff0) != VBE_DISPI_ID0) {
		DRM_ERROR("ID mismatch\n");
		return -ENODEV;
	}

	if ((pdev->resource[0].flags & IORESOURCE_MEM) == 0)
		return -ENODEV;
	addr = pci_resource_start(pdev, 0);
	size = pci_resource_len(pdev, 0);
	if (addr == 0)
		return -ENODEV;
	if (size != mem) {
		DRM_ERROR("Size mismatch: pci=%ld, bochs=%ld\n",
			size, mem);
		size = min(size, mem);
	}

	if (!devm_request_mem_region(&pdev->dev, addr, size, "bochs-drm"))
		DRM_WARN("Cannot request framebuffer, boot fb still active?\n");

	bochs->fb_map = devm_ioremap_wc(&pdev->dev, addr, size);
	if (bochs->fb_map == NULL) {
		DRM_ERROR("Cannot map framebuffer\n");
		return -ENOMEM;
	}
	bochs->fb_base = addr;
	bochs->fb_size = size;

	DRM_INFO("Found bochs VGA, ID 0x%x.\n", id);
	DRM_INFO("Framebuffer size %ld kB @ 0x%lx, %s @ 0x%lx.\n",
		 size / 1024, addr,
		 bochs->ioports ? "ioports" : "mmio",
		 ioaddr);

	if (bochs->mmio && pdev->revision >= 2) {
		bochs->qext_size = readl(bochs->mmio + 0x600);
		if (bochs->qext_size < 4 || bochs->qext_size > iosize) {
			bochs->qext_size = 0;
			goto noext;
		}
		DRM_DEBUG("Found qemu ext regs, size %ld\n",
			  bochs->qext_size);
		bochs_hw_set_native_endian(bochs);
	}

noext:
	return 0;
}

static void bochs_hw_blank(struct bochs_device *bochs, bool blank)
{
	DRM_DEBUG_DRIVER("hw_blank %d\n", blank);
	/* enable color bit (so VGA_IS1_RC access works) */
	bochs_vga_writeb(bochs, VGA_MIS_W, VGA_MIS_COLOR);
	/* discard ar_flip_flop */
	(void)bochs_vga_readb(bochs, VGA_IS1_RC);
	/* blank or unblank; we need only update index and set 0x20 */
	bochs_vga_writeb(bochs, VGA_ATT_W, blank ? 0 : 0x20);
}

static void bochs_hw_setmode(struct bochs_device *bochs, struct drm_display_mode *mode)
{
	int idx;

	if (!drm_dev_enter(&bochs->dev, &idx))
		return;

	bochs->xres = mode->hdisplay;
	bochs->yres = mode->vdisplay;
	bochs->bpp = 32;
	bochs->stride = mode->hdisplay * (bochs->bpp / 8);
	bochs->yres_virtual = bochs->fb_size / bochs->stride;

	DRM_DEBUG_DRIVER("%dx%d @ %d bpp, vy %d\n",
			 bochs->xres, bochs->yres, bochs->bpp,
			 bochs->yres_virtual);

	bochs_hw_blank(bochs, false);

	bochs_dispi_write(bochs, VBE_DISPI_INDEX_ENABLE,      0);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_BPP,         bochs->bpp);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_XRES,        bochs->xres);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_YRES,        bochs->yres);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_BANK,        0);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_VIRT_WIDTH,  bochs->xres);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_VIRT_HEIGHT,
			  bochs->yres_virtual);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_X_OFFSET,    0);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_Y_OFFSET,    0);

	bochs_dispi_write(bochs, VBE_DISPI_INDEX_ENABLE,
			  VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

	drm_dev_exit(idx);
}

static void bochs_hw_setformat(struct bochs_device *bochs, const struct drm_format_info *format)
{
	int idx;

	if (!drm_dev_enter(&bochs->dev, &idx))
		return;

	DRM_DEBUG_DRIVER("format %c%c%c%c\n",
			 (format->format >>  0) & 0xff,
			 (format->format >>  8) & 0xff,
			 (format->format >> 16) & 0xff,
			 (format->format >> 24) & 0xff);

	switch (format->format) {
	case DRM_FORMAT_XRGB8888:
		bochs_hw_set_little_endian(bochs);
		break;
	case DRM_FORMAT_BGRX8888:
		bochs_hw_set_big_endian(bochs);
		break;
	default:
		/* should not happen */
		DRM_ERROR("%s: Huh? Got framebuffer format 0x%x",
			  __func__, format->format);
		break;
	}

	drm_dev_exit(idx);
}

static void bochs_hw_setbase(struct bochs_device *bochs, int x, int y, int stride, u64 addr)
{
	unsigned long offset;
	unsigned int vx, vy, vwidth, idx;

	if (!drm_dev_enter(&bochs->dev, &idx))
		return;

	bochs->stride = stride;
	offset = (unsigned long)addr +
		y * bochs->stride +
		x * (bochs->bpp / 8);
	vy = offset / bochs->stride;
	vx = (offset % bochs->stride) * 8 / bochs->bpp;
	vwidth = stride * 8 / bochs->bpp;

	DRM_DEBUG_DRIVER("x %d, y %d, addr %llx -> offset %lx, vx %d, vy %d\n",
			 x, y, addr, offset, vx, vy);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_VIRT_WIDTH, vwidth);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_X_OFFSET, vx);
	bochs_dispi_write(bochs, VBE_DISPI_INDEX_Y_OFFSET, vy);

	drm_dev_exit(idx);
}

/* ---------------------------------------------------------------------- */

static const uint32_t bochs_primary_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_BGRX8888,
};

static int bochs_primary_plane_helper_atomic_check(struct drm_plane *plane,
						   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc *new_crtc = new_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state = NULL;
	int ret;

	if (new_crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;
	else if (!new_plane_state->visible)
		return 0;

	return 0;
}

static void bochs_primary_plane_helper_atomic_update(struct drm_plane *plane,
						     struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct bochs_device *bochs = to_bochs_device(dev);
	struct drm_plane_state *plane_state = plane->state;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;

	if (!fb || !bochs->stride)
		return;

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		struct iosys_map dst = IOSYS_MAP_INIT_VADDR_IOMEM(bochs->fb_map);

		iosys_map_incr(&dst, drm_fb_clip_offset(fb->pitches[0], fb->format, &damage));
		drm_fb_memcpy(&dst, fb->pitches, shadow_plane_state->data, fb, &damage);
	}

	/* Always scanout image at VRAM offset 0 */
	bochs_hw_setbase(bochs,
			 plane_state->crtc_x,
			 plane_state->crtc_y,
			 fb->pitches[0],
			 0);
	bochs_hw_setformat(bochs, fb->format);
}

static const struct drm_plane_helper_funcs bochs_primary_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = bochs_primary_plane_helper_atomic_check,
	.atomic_update = bochs_primary_plane_helper_atomic_update,
};

static const struct drm_plane_funcs bochs_primary_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS
};

static void bochs_crtc_helper_mode_set_nofb(struct drm_crtc *crtc)
{
	struct bochs_device *bochs = to_bochs_device(crtc->dev);
	struct drm_crtc_state *crtc_state = crtc->state;

	bochs_hw_setmode(bochs, &crtc_state->mode);
}

static int bochs_crtc_helper_atomic_check(struct drm_crtc *crtc,
					  struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->enable)
		return 0;

	return drm_atomic_helper_check_crtc_primary_plane(crtc_state);
}

static void bochs_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					    struct drm_atomic_state *state)
{
}

static void bochs_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					     struct drm_atomic_state *crtc_state)
{
	struct bochs_device *bochs = to_bochs_device(crtc->dev);

	bochs_hw_blank(bochs, true);
}

static const struct drm_crtc_helper_funcs bochs_crtc_helper_funcs = {
	.mode_set_nofb = bochs_crtc_helper_mode_set_nofb,
	.atomic_check = bochs_crtc_helper_atomic_check,
	.atomic_enable = bochs_crtc_helper_atomic_enable,
	.atomic_disable = bochs_crtc_helper_atomic_disable,
};

static const struct drm_crtc_funcs bochs_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_encoder_funcs bochs_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int bochs_connector_helper_get_modes(struct drm_connector *connector)
{
	const struct drm_edid *edid;
	int count;

	edid = bochs_hw_read_edid(connector);

	if (edid) {
		drm_edid_connector_update(connector, edid);
		count = drm_edid_connector_add_modes(connector);
		drm_edid_free(edid);
	} else {
		drm_edid_connector_update(connector, NULL);
		count = drm_add_modes_noedid(connector, 8192, 8192);
		drm_set_preferred_mode(connector, defx, defy);
	}

	return count;
}

static const struct drm_connector_helper_funcs bochs_connector_helper_funcs = {
	.get_modes = bochs_connector_helper_get_modes,
};

static const struct drm_connector_funcs bochs_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status bochs_mode_config_mode_valid(struct drm_device *dev,
							 const struct drm_display_mode *mode)
{
	struct bochs_device *bochs = to_bochs_device(dev);
	const struct drm_format_info *format = drm_format_info(DRM_FORMAT_XRGB8888);
	u64 pitch;

	if (drm_WARN_ON(dev, !format))
		return MODE_ERROR;

	pitch = drm_format_info_min_pitch(format, 0, mode->hdisplay);
	if (!pitch)
		return MODE_BAD_WIDTH;
	if (mode->vdisplay > DIV_ROUND_DOWN_ULL(bochs->fb_size, pitch))
		return MODE_MEM;

	return MODE_OK;
}

static const struct drm_mode_config_funcs bochs_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.mode_valid = bochs_mode_config_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int bochs_kms_init(struct bochs_device *bochs)
{
	struct drm_device *dev = &bochs->dev;
	struct drm_plane *primary_plane;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.quirk_addfb_prefer_host_byte_order = true;

	dev->mode_config.funcs = &bochs_mode_config_funcs;

	primary_plane = &bochs->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &bochs_primary_plane_funcs,
				       bochs_primary_plane_formats,
				       ARRAY_SIZE(bochs_primary_plane_formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(primary_plane, &bochs_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	crtc = &bochs->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&bochs_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &bochs_crtc_helper_funcs);

	encoder = &bochs->encoder;
	ret = drm_encoder_init(dev, encoder, &bochs_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	connector = &bochs->connector;
	ret = drm_connector_init(dev, connector, &bochs_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &bochs_connector_helper_funcs);
	drm_connector_attach_edid_property(connector);
	drm_connector_attach_encoder(connector, encoder);

	drm_mode_config_reset(dev);

	return 0;
}

/* ---------------------------------------------------------------------- */
/* drm interface                                                          */

static int bochs_load(struct bochs_device *bochs)
{
	int ret;

	ret = bochs_hw_init(bochs);
	if (ret)
		return ret;

	ret = bochs_kms_init(bochs);
	if (ret)
		return ret;

	return 0;
}

DEFINE_DRM_GEM_FOPS(bochs_fops);

static const struct drm_driver bochs_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &bochs_fops,
	.name			= "bochs-drm",
	.desc			= "bochs dispi vga interface (qemu stdvga)",
	.date			= "20130925",
	.major			= 1,
	.minor			= 0,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
};

/* ---------------------------------------------------------------------- */
/* pm interface                                                           */

#ifdef CONFIG_PM_SLEEP
static int bochs_pm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm_dev);
}

static int bochs_pm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm_dev);
}
#endif

static const struct dev_pm_ops bochs_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bochs_pm_suspend,
				bochs_pm_resume)
};

/* ---------------------------------------------------------------------- */
/* pci interface                                                          */

static int bochs_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct bochs_device *bochs;
	struct drm_device *dev;
	int ret;

	ret = aperture_remove_conflicting_pci_devices(pdev, bochs_driver.name);
	if (ret)
		return ret;

	bochs = devm_drm_dev_alloc(&pdev->dev, &bochs_driver, struct bochs_device, dev);
	if (IS_ERR(bochs))
		return PTR_ERR(bochs);
	dev = &bochs->dev;

	ret = pcim_enable_device(pdev);
	if (ret)
		goto err_free_dev;

	pci_set_drvdata(pdev, dev);

	ret = bochs_load(bochs);
	if (ret)
		goto err_free_dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free_dev;

	drm_client_setup(dev, NULL);

	return ret;

err_free_dev:
	drm_dev_put(dev);
	return ret;
}

static void bochs_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
	drm_dev_put(dev);
}

static void bochs_pci_shutdown(struct pci_dev *pdev)
{
	drm_atomic_helper_shutdown(pci_get_drvdata(pdev));
}

static const struct pci_device_id bochs_pci_tbl[] = {
	{
		.vendor      = 0x1234,
		.device      = 0x1111,
		.subvendor   = PCI_SUBVENDOR_ID_REDHAT_QUMRANET,
		.subdevice   = PCI_SUBDEVICE_ID_QEMU,
		.driver_data = BOCHS_QEMU_STDVGA,
	},
	{
		.vendor      = 0x1234,
		.device      = 0x1111,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = BOCHS_UNKNOWN,
	},
	{
		.vendor      = 0x4321,
		.device      = 0x1111,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = BOCHS_SIMICS,
	},
	{ /* end of list */ }
};

static struct pci_driver bochs_pci_driver = {
	.name =		"bochs-drm",
	.id_table =	bochs_pci_tbl,
	.probe =	bochs_pci_probe,
	.remove =	bochs_pci_remove,
	.shutdown =	bochs_pci_shutdown,
	.driver.pm =    &bochs_pm_ops,
};

/* ---------------------------------------------------------------------- */
/* module init/exit                                                       */

drm_module_pci_driver_if_modeset(bochs_pci_driver, bochs_modeset);

MODULE_DEVICE_TABLE(pci, bochs_pci_tbl);
MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
MODULE_DESCRIPTION("DRM Support for bochs dispi vga interface (qemu stdvga)");
MODULE_LICENSE("GPL");
