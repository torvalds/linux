// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/console.h>
#include <linux/pci.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

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
	BOCHS_UNKNOWN,
};

struct bochs_device {
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
	struct edid *edid;

	/* drm */
	struct drm_device *dev;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
};

/* ---------------------------------------------------------------------- */

static void bochs_vga_writeb(struct bochs_device *bochs, u16 ioport, u8 val)
{
	if (WARN_ON(ioport < 0x3c0 || ioport > 0x3df))
		return;

	if (bochs->mmio) {
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

	if (bochs->mmio) {
		int offset = ioport - 0x3c0 + 0x400;

		return readb(bochs->mmio + offset);
	} else {
		return inb(ioport);
	}
}

static u16 bochs_dispi_read(struct bochs_device *bochs, u16 reg)
{
	u16 ret = 0;

	if (bochs->mmio) {
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
	if (bochs->mmio) {
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

static int bochs_get_edid_block(void *data, u8 *buf,
				unsigned int block, size_t len)
{
	struct bochs_device *bochs = data;
	size_t i, start = block * EDID_LENGTH;

	if (start + len > 0x400 /* vga register offset */)
		return -1;

	for (i = 0; i < len; i++)
		buf[i] = readb(bochs->mmio + start + i);

	return 0;
}

static int bochs_hw_load_edid(struct bochs_device *bochs)
{
	u8 header[8];

	if (!bochs->mmio)
		return -1;

	/* check header to detect whenever edid support is enabled in qemu */
	bochs_get_edid_block(bochs, header, 0, ARRAY_SIZE(header));
	if (drm_edid_header_is_valid(header) != 8)
		return -1;

	kfree(bochs->edid);
	bochs->edid = drm_do_get_edid(&bochs->connector,
				      bochs_get_edid_block, bochs);
	if (bochs->edid == NULL)
		return -1;

	return 0;
}

static int bochs_hw_init(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned long addr, size, mem, ioaddr, iosize;
	u16 id;

	if (pdev->resource[2].flags & IORESOURCE_MEM) {
		/* mmio bar with vga and bochs registers present */
		if (pci_request_region(pdev, 2, "bochs-drm") != 0) {
			DRM_ERROR("Cannot request mmio region\n");
			return -EBUSY;
		}
		ioaddr = pci_resource_start(pdev, 2);
		iosize = pci_resource_len(pdev, 2);
		bochs->mmio = ioremap(ioaddr, iosize);
		if (bochs->mmio == NULL) {
			DRM_ERROR("Cannot map mmio region\n");
			return -ENOMEM;
		}
	} else {
		ioaddr = VBE_DISPI_IOPORT_INDEX;
		iosize = 2;
		if (!request_region(ioaddr, iosize, "bochs-drm")) {
			DRM_ERROR("Cannot request ioports\n");
			return -EBUSY;
		}
		bochs->ioports = 1;
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

	if (pci_request_region(pdev, 0, "bochs-drm") != 0)
		DRM_WARN("Cannot request framebuffer, boot fb still active?\n");

	bochs->fb_map = ioremap(addr, size);
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

static void bochs_hw_fini(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;

	/* TODO: shot down existing vram mappings */

	if (bochs->mmio)
		iounmap(bochs->mmio);
	if (bochs->ioports)
		release_region(VBE_DISPI_IOPORT_INDEX, 2);
	if (bochs->fb_map)
		iounmap(bochs->fb_map);
	pci_release_regions(to_pci_dev(dev->dev));
	kfree(bochs->edid);
}

static void bochs_hw_blank(struct bochs_device *bochs, bool blank)
{
	DRM_DEBUG_DRIVER("hw_blank %d\n", blank);
	/* discard ar_flip_flop */
	(void)bochs_vga_readb(bochs, VGA_IS1_RC);
	/* blank or unblank; we need only update index and set 0x20 */
	bochs_vga_writeb(bochs, VGA_ATT_W, blank ? 0 : 0x20);
}

static void bochs_hw_setmode(struct bochs_device *bochs, struct drm_display_mode *mode)
{
	int idx;

	if (!drm_dev_enter(bochs->dev, &idx))
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

	if (!drm_dev_enter(bochs->dev, &idx))
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

	if (!drm_dev_enter(bochs->dev, &idx))
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

static const uint32_t bochs_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_BGRX8888,
};

static void bochs_plane_update(struct bochs_device *bochs, struct drm_plane_state *state)
{
	struct drm_gem_vram_object *gbo;
	s64 gpu_addr;

	if (!state->fb || !bochs->stride)
		return;

	gbo = drm_gem_vram_of_gem(state->fb->obj[0]);
	gpu_addr = drm_gem_vram_offset(gbo);
	if (WARN_ON_ONCE(gpu_addr < 0))
		return; /* Bug: we didn't pin the BO to VRAM in prepare_fb. */

	bochs_hw_setbase(bochs,
			 state->crtc_x,
			 state->crtc_y,
			 state->fb->pitches[0],
			 state->fb->offsets[0] + gpu_addr);
	bochs_hw_setformat(bochs, state->fb->format);
}

static void bochs_pipe_enable(struct drm_simple_display_pipe *pipe,
			      struct drm_crtc_state *crtc_state,
			      struct drm_plane_state *plane_state)
{
	struct bochs_device *bochs = pipe->crtc.dev->dev_private;

	bochs_hw_setmode(bochs, &crtc_state->mode);
	bochs_plane_update(bochs, plane_state);
}

static void bochs_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct bochs_device *bochs = pipe->crtc.dev->dev_private;

	bochs_hw_blank(bochs, true);
}

static void bochs_pipe_update(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *old_state)
{
	struct bochs_device *bochs = pipe->crtc.dev->dev_private;

	bochs_plane_update(bochs, pipe->plane.state);
}

static const struct drm_simple_display_pipe_funcs bochs_pipe_funcs = {
	.enable	    = bochs_pipe_enable,
	.disable    = bochs_pipe_disable,
	.update	    = bochs_pipe_update,
	.prepare_fb = drm_gem_vram_simple_display_pipe_prepare_fb,
	.cleanup_fb = drm_gem_vram_simple_display_pipe_cleanup_fb,
};

static int bochs_connector_get_modes(struct drm_connector *connector)
{
	struct bochs_device *bochs =
		container_of(connector, struct bochs_device, connector);
	int count = 0;

	if (bochs->edid)
		count = drm_add_edid_modes(connector, bochs->edid);

	if (!count) {
		count = drm_add_modes_noedid(connector, 8192, 8192);
		drm_set_preferred_mode(connector, defx, defy);
	}
	return count;
}

static const struct drm_connector_helper_funcs bochs_connector_connector_helper_funcs = {
	.get_modes = bochs_connector_get_modes,
};

static const struct drm_connector_funcs bochs_connector_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void bochs_connector_init(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;
	struct drm_connector *connector = &bochs->connector;

	drm_connector_init(dev, connector, &bochs_connector_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	drm_connector_helper_add(connector, &bochs_connector_connector_helper_funcs);

	bochs_hw_load_edid(bochs);
	if (bochs->edid) {
		DRM_INFO("Found EDID data blob.\n");
		drm_connector_attach_edid_property(connector);
		drm_connector_update_edid_property(connector, bochs->edid);
	}
}

static struct drm_framebuffer *
bochs_gem_fb_create(struct drm_device *dev, struct drm_file *file,
		    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	if (mode_cmd->pixel_format != DRM_FORMAT_XRGB8888 &&
	    mode_cmd->pixel_format != DRM_FORMAT_BGRX8888)
		return ERR_PTR(-EINVAL);

	return drm_gem_fb_create(dev, file, mode_cmd);
}

static const struct drm_mode_config_funcs bochs_mode_funcs = {
	.fb_create = bochs_gem_fb_create,
	.mode_valid = drm_vram_helper_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int bochs_kms_init(struct bochs_device *bochs)
{
	int ret;

	ret = drmm_mode_config_init(bochs->dev);
	if (ret)
		return ret;

	bochs->dev->mode_config.max_width = 8192;
	bochs->dev->mode_config.max_height = 8192;

	bochs->dev->mode_config.fb_base = bochs->fb_base;
	bochs->dev->mode_config.preferred_depth = 24;
	bochs->dev->mode_config.prefer_shadow = 0;
	bochs->dev->mode_config.prefer_shadow_fbdev = 1;
	bochs->dev->mode_config.quirk_addfb_prefer_host_byte_order = true;

	bochs->dev->mode_config.funcs = &bochs_mode_funcs;

	bochs_connector_init(bochs->dev);
	drm_simple_display_pipe_init(bochs->dev,
				     &bochs->pipe,
				     &bochs_pipe_funcs,
				     bochs_formats,
				     ARRAY_SIZE(bochs_formats),
				     NULL,
				     &bochs->connector);

	drm_mode_config_reset(bochs->dev);

	return 0;
}

/* ---------------------------------------------------------------------- */
/* drm interface                                                          */

static int bochs_load(struct drm_device *dev)
{
	struct bochs_device *bochs;
	int ret;

	bochs = drmm_kzalloc(dev, sizeof(*bochs), GFP_KERNEL);
	if (bochs == NULL)
		return -ENOMEM;
	dev->dev_private = bochs;
	bochs->dev = dev;

	ret = bochs_hw_init(dev);
	if (ret)
		return ret;

	ret = drmm_vram_helper_init(dev, bochs->fb_base, bochs->fb_size);
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
	DRM_GEM_VRAM_DRIVER,
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
	struct drm_device *dev;
	unsigned long fbsize;
	int ret;

	fbsize = pci_resource_len(pdev, 0);
	if (fbsize < 4 * 1024 * 1024) {
		DRM_ERROR("less than 4 MB video memory, ignoring device\n");
		return -ENOMEM;
	}

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &bochs_driver);
	if (ret)
		return ret;

	dev = drm_dev_alloc(&bochs_driver, &pdev->dev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = pcim_enable_device(pdev);
	if (ret)
		goto err_free_dev;

	pci_set_drvdata(pdev, dev);

	ret = bochs_load(dev);
	if (ret)
		goto err_free_dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free_dev;

	drm_fbdev_generic_setup(dev, 32);
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
	bochs_hw_fini(dev);
	drm_dev_put(dev);
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
	{ /* end of list */ }
};

static struct pci_driver bochs_pci_driver = {
	.name =		"bochs-drm",
	.id_table =	bochs_pci_tbl,
	.probe =	bochs_pci_probe,
	.remove =	bochs_pci_remove,
	.driver.pm =    &bochs_pm_ops,
};

/* ---------------------------------------------------------------------- */
/* module init/exit                                                       */

static int __init bochs_init(void)
{
	if (vgacon_text_force() && bochs_modeset == -1)
		return -EINVAL;

	if (bochs_modeset == 0)
		return -EINVAL;

	return pci_register_driver(&bochs_pci_driver);
}

static void __exit bochs_exit(void)
{
	pci_unregister_driver(&bochs_pci_driver);
}

module_init(bochs_init);
module_exit(bochs_exit);

MODULE_DEVICE_TABLE(pci, bochs_pci_tbl);
MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
MODULE_LICENSE("GPL");
