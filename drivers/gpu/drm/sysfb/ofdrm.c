// SPDX-License-Identifier: GPL-2.0-only

#include <linux/aperture.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include "drm_sysfb_helper.h"

#define DRIVER_NAME	"ofdrm"
#define DRIVER_DESC	"DRM driver for OF platform devices"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define PCI_VENDOR_ID_ATI_R520	0x7100
#define PCI_VENDOR_ID_ATI_R600	0x9400

#define OFDRM_GAMMA_LUT_SIZE	256

/* Definitions used by the Avivo palette  */
#define AVIVO_DC_LUT_RW_SELECT                  0x6480
#define AVIVO_DC_LUT_RW_MODE                    0x6484
#define AVIVO_DC_LUT_RW_INDEX                   0x6488
#define AVIVO_DC_LUT_SEQ_COLOR                  0x648c
#define AVIVO_DC_LUT_PWL_DATA                   0x6490
#define AVIVO_DC_LUT_30_COLOR                   0x6494
#define AVIVO_DC_LUT_READ_PIPE_SELECT           0x6498
#define AVIVO_DC_LUT_WRITE_EN_MASK              0x649c
#define AVIVO_DC_LUT_AUTOFILL                   0x64a0
#define AVIVO_DC_LUTA_CONTROL                   0x64c0
#define AVIVO_DC_LUTA_BLACK_OFFSET_BLUE         0x64c4
#define AVIVO_DC_LUTA_BLACK_OFFSET_GREEN        0x64c8
#define AVIVO_DC_LUTA_BLACK_OFFSET_RED          0x64cc
#define AVIVO_DC_LUTA_WHITE_OFFSET_BLUE         0x64d0
#define AVIVO_DC_LUTA_WHITE_OFFSET_GREEN        0x64d4
#define AVIVO_DC_LUTA_WHITE_OFFSET_RED          0x64d8
#define AVIVO_DC_LUTB_CONTROL                   0x6cc0
#define AVIVO_DC_LUTB_BLACK_OFFSET_BLUE         0x6cc4
#define AVIVO_DC_LUTB_BLACK_OFFSET_GREEN        0x6cc8
#define AVIVO_DC_LUTB_BLACK_OFFSET_RED          0x6ccc
#define AVIVO_DC_LUTB_WHITE_OFFSET_BLUE         0x6cd0
#define AVIVO_DC_LUTB_WHITE_OFFSET_GREEN        0x6cd4
#define AVIVO_DC_LUTB_WHITE_OFFSET_RED          0x6cd8

enum ofdrm_model {
	OFDRM_MODEL_UNKNOWN,
	OFDRM_MODEL_MACH64, /* ATI Mach64 */
	OFDRM_MODEL_RAGE128, /* ATI Rage128 */
	OFDRM_MODEL_RAGE_M3A, /* ATI Rage Mobility M3 Head A */
	OFDRM_MODEL_RAGE_M3B, /* ATI Rage Mobility M3 Head B */
	OFDRM_MODEL_RADEON, /* ATI Radeon */
	OFDRM_MODEL_GXT2000, /* IBM GXT2000 */
	OFDRM_MODEL_AVIVO, /* ATI R5xx */
	OFDRM_MODEL_QEMU, /* QEMU VGA */
};

/*
 * Helpers for display nodes
 */

static int display_get_validated_int(struct drm_device *dev, const char *name, uint32_t value)
{
	return drm_sysfb_get_validated_int(dev, name, value, INT_MAX);
}

static int display_get_validated_int0(struct drm_device *dev, const char *name, uint32_t value)
{
	return drm_sysfb_get_validated_int0(dev, name, value, INT_MAX);
}

static const struct drm_format_info *display_get_validated_format(struct drm_device *dev,
								  u32 depth, bool big_endian)
{
	const struct drm_format_info *info;
	u32 format;

	switch (depth) {
	case 8:
		format = drm_mode_legacy_fb_format(8, 8);
		break;
	case 15:
	case 16:
		format = drm_mode_legacy_fb_format(16, depth);
		break;
	case 32:
		format = drm_mode_legacy_fb_format(32, 24);
		break;
	default:
		drm_err(dev, "unsupported framebuffer depth %u\n", depth);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * DRM formats assume little-endian byte order. Update the format
	 * if the scanout buffer uses big-endian ordering.
	 */
	if (big_endian) {
		switch (format) {
		case DRM_FORMAT_XRGB8888:
			format = DRM_FORMAT_BGRX8888;
			break;
		case DRM_FORMAT_ARGB8888:
			format = DRM_FORMAT_BGRA8888;
			break;
		case DRM_FORMAT_RGB565:
			format = DRM_FORMAT_RGB565 | DRM_FORMAT_BIG_ENDIAN;
			break;
		case DRM_FORMAT_XRGB1555:
			format = DRM_FORMAT_XRGB1555 | DRM_FORMAT_BIG_ENDIAN;
			break;
		default:
			break;
		}
	}

	info = drm_format_info(format);
	if (!info) {
		drm_err(dev, "cannot find framebuffer format for depth %u\n", depth);
		return ERR_PTR(-EINVAL);
	}

	return info;
}

static int display_read_u32_of(struct drm_device *dev, struct device_node *of_node,
			       const char *name, u32 *value)
{
	int ret = of_property_read_u32(of_node, name, value);

	if (ret)
		drm_err(dev, "cannot parse framebuffer %s: error %d\n", name, ret);
	return ret;
}

static bool display_get_big_endian_of(struct drm_device *dev, struct device_node *of_node)
{
	bool big_endian;

#ifdef __BIG_ENDIAN
	big_endian = !of_property_read_bool(of_node, "little-endian");
#else
	big_endian = of_property_read_bool(of_node, "big-endian");
#endif

	return big_endian;
}

static int display_get_width_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 width;
	int ret = display_read_u32_of(dev, of_node, "width", &width);

	if (ret)
		return ret;
	return display_get_validated_int0(dev, "width", width);
}

static int display_get_height_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 height;
	int ret = display_read_u32_of(dev, of_node, "height", &height);

	if (ret)
		return ret;
	return display_get_validated_int0(dev, "height", height);
}

static int display_get_depth_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 depth;
	int ret = display_read_u32_of(dev, of_node, "depth", &depth);

	if (ret)
		return ret;
	return display_get_validated_int0(dev, "depth", depth);
}

static int display_get_linebytes_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 linebytes;
	int ret = display_read_u32_of(dev, of_node, "linebytes", &linebytes);

	if (ret)
		return ret;
	return display_get_validated_int(dev, "linebytes", linebytes);
}

static u64 display_get_address_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 address;
	int ret;

	/*
	 * Not all devices provide an address property, it's not
	 * a bug if this fails. The driver will try to find the
	 * framebuffer base address from the device's memory regions.
	 */
	ret = of_property_read_u32(of_node, "address", &address);
	if (ret)
		return OF_BAD_ADDR;

	return address;
}

static const u8 *display_get_edid_of(struct drm_device *dev, struct device_node *of_node,
				     u8 buf[EDID_LENGTH])
{
	int ret = of_property_read_u8_array(of_node, "EDID", buf, EDID_LENGTH);

	if (ret)
		return NULL;
	return buf;
}

static bool is_avivo(u32 vendor, u32 device)
{
	/* This will match most R5xx */
	return (vendor == PCI_VENDOR_ID_ATI) &&
	       ((device >= PCI_VENDOR_ID_ATI_R520 && device < 0x7800) ||
		(PCI_VENDOR_ID_ATI_R600 >= 0x9400));
}

static enum ofdrm_model display_get_model_of(struct drm_device *dev, struct device_node *of_node)
{
	enum ofdrm_model model = OFDRM_MODEL_UNKNOWN;

	if (of_node_name_prefix(of_node, "ATY,Rage128")) {
		model = OFDRM_MODEL_RAGE128;
	} else if (of_node_name_prefix(of_node, "ATY,RageM3pA") ||
		   of_node_name_prefix(of_node, "ATY,RageM3p12A")) {
		model = OFDRM_MODEL_RAGE_M3A;
	} else if (of_node_name_prefix(of_node, "ATY,RageM3pB")) {
		model = OFDRM_MODEL_RAGE_M3B;
	} else if (of_node_name_prefix(of_node, "ATY,Rage6")) {
		model = OFDRM_MODEL_RADEON;
	} else if (of_node_name_prefix(of_node, "ATY,")) {
		return OFDRM_MODEL_MACH64;
	} else if (of_device_is_compatible(of_node, "pci1014,b7") ||
		   of_device_is_compatible(of_node, "pci1014,21c")) {
		model = OFDRM_MODEL_GXT2000;
	} else if (of_node_name_prefix(of_node, "vga,Display-")) {
		struct device_node *of_parent;
		const __be32 *vendor_p, *device_p;

		/* Look for AVIVO initialized by SLOF */
		of_parent = of_get_parent(of_node);
		vendor_p = of_get_property(of_parent, "vendor-id", NULL);
		device_p = of_get_property(of_parent, "device-id", NULL);
		if (vendor_p && device_p) {
			u32 vendor = be32_to_cpup(vendor_p);
			u32 device = be32_to_cpup(device_p);

			if (is_avivo(vendor, device))
				model = OFDRM_MODEL_AVIVO;
		}
		of_node_put(of_parent);
	} else if (of_device_is_compatible(of_node, "qemu,std-vga")) {
		model = OFDRM_MODEL_QEMU;
	}

	return model;
}

/*
 * Open Firmware display device
 */

struct ofdrm_device;

struct ofdrm_device_funcs {
	void __iomem *(*cmap_ioremap)(struct ofdrm_device *odev,
				      struct device_node *of_node,
				      u64 fb_bas);
	void (*cmap_write)(struct ofdrm_device *odev, unsigned char index,
			   unsigned char r, unsigned char g, unsigned char b);
};

struct ofdrm_device {
	struct drm_sysfb_device sysfb;

	const struct ofdrm_device_funcs *funcs;

	/* colormap */
	void __iomem *cmap_base;

	u8 edid[EDID_LENGTH];

	/* modesetting */
	u32 formats[DRM_SYSFB_PLANE_NFORMATS(1)];
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct ofdrm_device *ofdrm_device_of_dev(struct drm_device *dev)
{
	return container_of(to_drm_sysfb_device(dev), struct ofdrm_device, sysfb);
}

/*
 * Hardware
 */

#if defined(CONFIG_PCI)
static struct pci_dev *display_get_pci_dev_of(struct drm_device *dev, struct device_node *of_node)
{
	const __be32 *vendor_p, *device_p;
	u32 vendor, device;
	struct pci_dev *pcidev;

	vendor_p = of_get_property(of_node, "vendor-id", NULL);
	if (!vendor_p)
		return ERR_PTR(-ENODEV);
	vendor = be32_to_cpup(vendor_p);

	device_p = of_get_property(of_node, "device-id", NULL);
	if (!device_p)
		return ERR_PTR(-ENODEV);
	device = be32_to_cpup(device_p);

	pcidev = pci_get_device(vendor, device, NULL);
	if (!pcidev)
		return ERR_PTR(-ENODEV);

	return pcidev;
}

static void ofdrm_pci_release(void *data)
{
	struct pci_dev *pcidev = data;

	pci_disable_device(pcidev);
}

static int ofdrm_device_init_pci(struct ofdrm_device *odev)
{
	struct drm_device *dev = &odev->sysfb.dev;
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct device_node *of_node = pdev->dev.of_node;
	struct pci_dev *pcidev;
	int ret;

	/*
	 * Never use pcim_ or other managed helpers on the returned PCI
	 * device. Otherwise, probing the native driver will fail for
	 * resource conflicts. PCI-device management has to be tied to
	 * the lifetime of the platform device until the native driver
	 * takes over.
	 */
	pcidev = display_get_pci_dev_of(dev, of_node);
	if (IS_ERR(pcidev))
		return 0; /* no PCI device found; ignore the error */

	ret = pci_enable_device(pcidev);
	if (ret) {
		drm_err(dev, "pci_enable_device(%s) failed: %d\n",
			dev_name(&pcidev->dev), ret);
		return ret;
	}
	ret = devm_add_action_or_reset(&pdev->dev, ofdrm_pci_release, pcidev);
	if (ret)
		return ret;

	return 0;
}
#else
static int ofdrm_device_init_pci(struct ofdrm_device *odev)
{
	return 0;
}
#endif

/*
 *  OF display settings
 */

static struct resource *ofdrm_find_fb_resource(struct ofdrm_device *odev,
					       struct resource *fb_res)
{
	struct platform_device *pdev = to_platform_device(odev->sysfb.dev.dev);
	struct resource *res, *max_res = NULL;
	u32 i;

	for (i = 0; pdev->num_resources; ++i) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break; /* all resources processed */
		if (resource_size(res) < resource_size(fb_res))
			continue; /* resource too small */
		if (fb_res->start && resource_contains(res, fb_res))
			return res; /* resource contains framebuffer */
		if (!max_res || resource_size(res) > resource_size(max_res))
			max_res = res; /* store largest resource as fallback */
	}

	return max_res;
}

/*
 * Colormap / Palette
 */

static void __iomem *get_cmap_address_of(struct ofdrm_device *odev, struct device_node *of_node,
					 int bar_no, unsigned long offset, unsigned long size)
{
	struct drm_device *dev = &odev->sysfb.dev;
	const __be32 *addr_p;
	u64 max_size, address;
	unsigned int flags;
	void __iomem *mem;

	addr_p = of_get_pci_address(of_node, bar_no, &max_size, &flags);
	if (!addr_p)
		addr_p = of_get_address(of_node, bar_no, &max_size, &flags);
	if (!addr_p)
		return IOMEM_ERR_PTR(-ENODEV);

	if ((flags & (IORESOURCE_IO | IORESOURCE_MEM)) == 0)
		return IOMEM_ERR_PTR(-ENODEV);

	if ((offset + size) >= max_size)
		return IOMEM_ERR_PTR(-ENODEV);

	address = of_translate_address(of_node, addr_p);
	if (address == OF_BAD_ADDR)
		return IOMEM_ERR_PTR(-ENODEV);

	mem = devm_ioremap(dev->dev, address + offset, size);
	if (!mem)
		return IOMEM_ERR_PTR(-ENOMEM);

	return mem;
}

static void __iomem *ofdrm_mach64_cmap_ioremap(struct ofdrm_device *odev,
					       struct device_node *of_node,
					       u64 fb_base)
{
	struct drm_device *dev = &odev->sysfb.dev;
	u64 address;
	void __iomem *cmap_base;

	address = fb_base & 0xff000000ul;
	address += 0x7ff000;

	cmap_base = devm_ioremap(dev->dev, address, 0x1000);
	if (!cmap_base)
		return IOMEM_ERR_PTR(-ENOMEM);

	return cmap_base;
}

static void ofdrm_mach64_cmap_write(struct ofdrm_device *odev, unsigned char index,
				    unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *addr = odev->cmap_base + 0xcc0;
	void __iomem *data = odev->cmap_base + 0xcc0 + 1;

	writeb(index, addr);
	writeb(r, data);
	writeb(g, data);
	writeb(b, data);
}

static void __iomem *ofdrm_rage128_cmap_ioremap(struct ofdrm_device *odev,
						struct device_node *of_node,
						u64 fb_base)
{
	return get_cmap_address_of(odev, of_node, 2, 0, 0x1fff);
}

static void ofdrm_rage128_cmap_write(struct ofdrm_device *odev, unsigned char index,
				     unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *addr = odev->cmap_base + 0xb0;
	void __iomem *data = odev->cmap_base + 0xb4;
	u32 color = (r << 16) | (g << 8) | b;

	writeb(index, addr);
	writel(color, data);
}

static void __iomem *ofdrm_rage_m3a_cmap_ioremap(struct ofdrm_device *odev,
						 struct device_node *of_node,
						 u64 fb_base)
{
	return get_cmap_address_of(odev, of_node, 2, 0, 0x1fff);
}

static void ofdrm_rage_m3a_cmap_write(struct ofdrm_device *odev, unsigned char index,
				      unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *dac_ctl = odev->cmap_base + 0x58;
	void __iomem *addr = odev->cmap_base + 0xb0;
	void __iomem *data = odev->cmap_base + 0xb4;
	u32 color = (r << 16) | (g << 8) | b;
	u32 val;

	/* Clear PALETTE_ACCESS_CNTL in DAC_CNTL */
	val = readl(dac_ctl);
	val &= ~0x20;
	writel(val, dac_ctl);

	/* Set color at palette index */
	writeb(index, addr);
	writel(color, data);
}

static void __iomem *ofdrm_rage_m3b_cmap_ioremap(struct ofdrm_device *odev,
						 struct device_node *of_node,
						 u64 fb_base)
{
	return get_cmap_address_of(odev, of_node, 2, 0, 0x1fff);
}

static void ofdrm_rage_m3b_cmap_write(struct ofdrm_device *odev, unsigned char index,
				      unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *dac_ctl = odev->cmap_base + 0x58;
	void __iomem *addr = odev->cmap_base + 0xb0;
	void __iomem *data = odev->cmap_base + 0xb4;
	u32 color = (r << 16) | (g << 8) | b;
	u32 val;

	/* Set PALETTE_ACCESS_CNTL in DAC_CNTL */
	val = readl(dac_ctl);
	val |= 0x20;
	writel(val, dac_ctl);

	/* Set color at palette index */
	writeb(index, addr);
	writel(color, data);
}

static void __iomem *ofdrm_radeon_cmap_ioremap(struct ofdrm_device *odev,
					       struct device_node *of_node,
					       u64 fb_base)
{
	return get_cmap_address_of(odev, of_node, 1, 0, 0x1fff);
}

static void __iomem *ofdrm_gxt2000_cmap_ioremap(struct ofdrm_device *odev,
						struct device_node *of_node,
						u64 fb_base)
{
	return get_cmap_address_of(odev, of_node, 0, 0x6000, 0x1000);
}

static void ofdrm_gxt2000_cmap_write(struct ofdrm_device *odev, unsigned char index,
				     unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *data = ((unsigned int __iomem *)odev->cmap_base) + index;
	u32 color = (r << 16) | (g << 8) | b;

	writel(color, data);
}

static void __iomem *ofdrm_avivo_cmap_ioremap(struct ofdrm_device *odev,
					      struct device_node *of_node,
					      u64 fb_base)
{
	struct device_node *of_parent;
	void __iomem *cmap_base;

	of_parent = of_get_parent(of_node);
	cmap_base = get_cmap_address_of(odev, of_parent, 0, 0, 0x10000);
	of_node_put(of_parent);

	return cmap_base;
}

static void ofdrm_avivo_cmap_write(struct ofdrm_device *odev, unsigned char index,
				   unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *lutsel = odev->cmap_base + AVIVO_DC_LUT_RW_SELECT;
	void __iomem *addr = odev->cmap_base + AVIVO_DC_LUT_RW_INDEX;
	void __iomem *data = odev->cmap_base + AVIVO_DC_LUT_30_COLOR;
	u32 color = (r << 22) | (g << 12) | (b << 2);

	/* Write to both LUTs for now */

	writel(1, lutsel);
	writeb(index, addr);
	writel(color, data);

	writel(0, lutsel);
	writeb(index, addr);
	writel(color, data);
}

static void __iomem *ofdrm_qemu_cmap_ioremap(struct ofdrm_device *odev,
					     struct device_node *of_node,
					     u64 fb_base)
{
	static const __be32 io_of_addr[3] = {
		cpu_to_be32(0x01000000),
		cpu_to_be32(0x00),
		cpu_to_be32(0x00),
	};

	struct drm_device *dev = &odev->sysfb.dev;
	u64 address;
	void __iomem *cmap_base;

	address = of_translate_address(of_node, io_of_addr);
	if (address == OF_BAD_ADDR)
		return IOMEM_ERR_PTR(-ENODEV);

	cmap_base = devm_ioremap(dev->dev, address + 0x3c8, 2);
	if (!cmap_base)
		return IOMEM_ERR_PTR(-ENOMEM);

	return cmap_base;
}

static void ofdrm_qemu_cmap_write(struct ofdrm_device *odev, unsigned char index,
				  unsigned char r, unsigned char g, unsigned char b)
{
	void __iomem *addr = odev->cmap_base;
	void __iomem *data = odev->cmap_base + 1;

	writeb(index, addr);
	writeb(r, data);
	writeb(g, data);
	writeb(b, data);
}

static void ofdrm_set_gamma_lut(struct drm_crtc *crtc, unsigned int index,
				u16 red, u16 green, u16 blue)
{
	struct drm_device *dev = crtc->dev;
	struct ofdrm_device *odev = ofdrm_device_of_dev(dev);
	u8 i8 = index & 0xff;
	u8 r8 = red >> 8;
	u8 g8 = green >> 8;
	u8 b8 = blue >> 8;

	if (drm_WARN_ON_ONCE(dev, index != i8))
		return; /* driver bug */

	odev->funcs->cmap_write(odev, i8, r8, g8, b8);
}

static void ofdrm_device_fill_gamma(struct ofdrm_device *odev,
				    const struct drm_format_info *format)
{
	struct drm_device *dev = &odev->sysfb.dev;
	struct drm_crtc *crtc = &odev->crtc;

	switch (format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB565 | DRM_FORMAT_BIG_ENDIAN:
		drm_crtc_fill_gamma_565(crtc, ofdrm_set_gamma_lut);
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGRX8888:
		drm_crtc_fill_gamma_888(crtc, ofdrm_set_gamma_lut);
		break;
	default:
		drm_warn_once(dev, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

static void ofdrm_device_load_gamma(struct ofdrm_device *odev,
				    const struct drm_format_info *format,
				    struct drm_color_lut *lut)
{
	struct drm_device *dev = &odev->sysfb.dev;
	struct drm_crtc *crtc = &odev->crtc;

	switch (format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB565 | DRM_FORMAT_BIG_ENDIAN:
		drm_crtc_load_gamma_565_from_888(crtc, lut, ofdrm_set_gamma_lut);
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGRX8888:
		drm_crtc_load_gamma_888(crtc, lut, ofdrm_set_gamma_lut);
		break;
	default:
		drm_warn_once(dev, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

/*
 * Modesetting
 */

static const u64 ofdrm_primary_plane_format_modifiers[] = {
	DRM_SYSFB_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs ofdrm_primary_plane_helper_funcs = {
	DRM_SYSFB_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs ofdrm_primary_plane_funcs = {
	DRM_SYSFB_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static void ofdrm_crtc_helper_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct ofdrm_device *odev = ofdrm_device_of_dev(crtc->dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_sysfb_crtc_state *sysfb_crtc_state = to_drm_sysfb_crtc_state(crtc_state);

	if (crtc_state->enable && crtc_state->color_mgmt_changed) {
		const struct drm_format_info *format = sysfb_crtc_state->format;

		if (crtc_state->gamma_lut)
			ofdrm_device_load_gamma(odev, format, crtc_state->gamma_lut->data);
		else
			ofdrm_device_fill_gamma(odev, format);
	}
}

static const struct drm_crtc_helper_funcs ofdrm_crtc_helper_funcs = {
	DRM_SYSFB_CRTC_HELPER_FUNCS,
	.atomic_flush = ofdrm_crtc_helper_atomic_flush,
};

static const struct drm_crtc_funcs ofdrm_crtc_funcs = {
	DRM_SYSFB_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs ofdrm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs ofdrm_connector_helper_funcs = {
	DRM_SYSFB_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs ofdrm_connector_funcs = {
	DRM_SYSFB_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_funcs ofdrm_mode_config_funcs = {
	DRM_SYSFB_MODE_CONFIG_FUNCS,
};

/*
 * Init / Cleanup
 */

static const struct ofdrm_device_funcs ofdrm_unknown_device_funcs = {
};

static const struct ofdrm_device_funcs ofdrm_mach64_device_funcs = {
	.cmap_ioremap = ofdrm_mach64_cmap_ioremap,
	.cmap_write = ofdrm_mach64_cmap_write,
};

static const struct ofdrm_device_funcs ofdrm_rage128_device_funcs = {
	.cmap_ioremap = ofdrm_rage128_cmap_ioremap,
	.cmap_write = ofdrm_rage128_cmap_write,
};

static const struct ofdrm_device_funcs ofdrm_rage_m3a_device_funcs = {
	.cmap_ioremap = ofdrm_rage_m3a_cmap_ioremap,
	.cmap_write = ofdrm_rage_m3a_cmap_write,
};

static const struct ofdrm_device_funcs ofdrm_rage_m3b_device_funcs = {
	.cmap_ioremap = ofdrm_rage_m3b_cmap_ioremap,
	.cmap_write = ofdrm_rage_m3b_cmap_write,
};

static const struct ofdrm_device_funcs ofdrm_radeon_device_funcs = {
	.cmap_ioremap = ofdrm_radeon_cmap_ioremap,
	.cmap_write = ofdrm_rage128_cmap_write, /* same as Rage128 */
};

static const struct ofdrm_device_funcs ofdrm_gxt2000_device_funcs = {
	.cmap_ioremap = ofdrm_gxt2000_cmap_ioremap,
	.cmap_write = ofdrm_gxt2000_cmap_write,
};

static const struct ofdrm_device_funcs ofdrm_avivo_device_funcs = {
	.cmap_ioremap = ofdrm_avivo_cmap_ioremap,
	.cmap_write = ofdrm_avivo_cmap_write,
};

static const struct ofdrm_device_funcs ofdrm_qemu_device_funcs = {
	.cmap_ioremap = ofdrm_qemu_cmap_ioremap,
	.cmap_write = ofdrm_qemu_cmap_write,
};

static struct ofdrm_device *ofdrm_device_create(struct drm_driver *drv,
						struct platform_device *pdev)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct ofdrm_device *odev;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	enum ofdrm_model model;
	bool big_endian;
	int width, height, depth, linebytes;
	const struct drm_format_info *format;
	u64 address;
	const u8 *edid;
	resource_size_t fb_size, fb_base, fb_pgbase, fb_pgsize;
	struct resource *res, *mem;
	void __iomem *screen_base;
	struct drm_plane *primary_plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	unsigned long max_width, max_height;
	size_t nformats;
	int ret;

	odev = devm_drm_dev_alloc(&pdev->dev, drv, struct ofdrm_device, sysfb.dev);
	if (IS_ERR(odev))
		return ERR_CAST(odev);
	sysfb = &odev->sysfb;
	dev = &sysfb->dev;
	platform_set_drvdata(pdev, dev);

	ret = ofdrm_device_init_pci(odev);
	if (ret)
		return ERR_PTR(ret);

	/*
	 * OF display-node settings
	 */

	model = display_get_model_of(dev, of_node);
	drm_dbg(dev, "detected model %d\n", model);

	switch (model) {
	case OFDRM_MODEL_UNKNOWN:
		odev->funcs = &ofdrm_unknown_device_funcs;
		break;
	case OFDRM_MODEL_MACH64:
		odev->funcs = &ofdrm_mach64_device_funcs;
		break;
	case OFDRM_MODEL_RAGE128:
		odev->funcs = &ofdrm_rage128_device_funcs;
		break;
	case OFDRM_MODEL_RAGE_M3A:
		odev->funcs = &ofdrm_rage_m3a_device_funcs;
		break;
	case OFDRM_MODEL_RAGE_M3B:
		odev->funcs = &ofdrm_rage_m3b_device_funcs;
		break;
	case OFDRM_MODEL_RADEON:
		odev->funcs = &ofdrm_radeon_device_funcs;
		break;
	case OFDRM_MODEL_GXT2000:
		odev->funcs = &ofdrm_gxt2000_device_funcs;
		break;
	case OFDRM_MODEL_AVIVO:
		odev->funcs = &ofdrm_avivo_device_funcs;
		break;
	case OFDRM_MODEL_QEMU:
		odev->funcs = &ofdrm_qemu_device_funcs;
		break;
	}

	big_endian = display_get_big_endian_of(dev, of_node);

	width = display_get_width_of(dev, of_node);
	if (width < 0)
		return ERR_PTR(width);
	height = display_get_height_of(dev, of_node);
	if (height < 0)
		return ERR_PTR(height);
	depth = display_get_depth_of(dev, of_node);
	if (depth < 0)
		return ERR_PTR(depth);
	linebytes = display_get_linebytes_of(dev, of_node);
	if (linebytes < 0)
		return ERR_PTR(linebytes);

	format = display_get_validated_format(dev, depth, big_endian);
	if (IS_ERR(format))
		return ERR_CAST(format);
	if (!linebytes) {
		linebytes = drm_format_info_min_pitch(format, 0, width);
		if (drm_WARN_ON(dev, !linebytes))
			return ERR_PTR(-EINVAL);
	}

	fb_size = linebytes * height;

	/*
	 * Try to figure out the address of the framebuffer. Unfortunately, Open
	 * Firmware doesn't provide a standard way to do so. All we can do is a
	 * dodgy heuristic that happens to work in practice.
	 *
	 * On most machines, the "address" property contains what we need, though
	 * not on Matrox cards found in IBM machines. What appears to give good
	 * results is to go through the PCI ranges and pick one that encloses the
	 * "address" property. If none match, we pick the largest.
	 */
	address = display_get_address_of(dev, of_node);
	if (address != OF_BAD_ADDR) {
		struct resource fb_res = DEFINE_RES_MEM(address, fb_size);

		res = ofdrm_find_fb_resource(odev, &fb_res);
		if (!res)
			return ERR_PTR(-EINVAL);
		if (resource_contains(res, &fb_res))
			fb_base = address;
		else
			fb_base = res->start;
	} else {
		struct resource fb_res = DEFINE_RES_MEM(0u, fb_size);

		res = ofdrm_find_fb_resource(odev, &fb_res);
		if (!res)
			return ERR_PTR(-EINVAL);
		fb_base = res->start;
	}

	/*
	 * I/O resources
	 */

	fb_pgbase = round_down(fb_base, PAGE_SIZE);
	fb_pgsize = fb_base - fb_pgbase + round_up(fb_size, PAGE_SIZE);

	ret = devm_aperture_acquire_for_platform_device(pdev, fb_pgbase, fb_pgsize);
	if (ret) {
		drm_err(dev, "could not acquire memory range %pr: error %d\n", &res, ret);
		return ERR_PTR(ret);
	}

	mem = devm_request_mem_region(&pdev->dev, fb_pgbase, fb_pgsize, drv->name);
	if (!mem) {
		drm_warn(dev, "could not acquire memory region %pr\n", &res);
		return ERR_PTR(-ENOMEM);
	}

	screen_base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!screen_base)
		return ERR_PTR(-ENOMEM);

	if (odev->funcs->cmap_ioremap) {
		void __iomem *cmap_base = odev->funcs->cmap_ioremap(odev, of_node, fb_base);

		if (IS_ERR(cmap_base)) {
			/* Don't fail; continue without colormap */
			drm_warn(dev, "could not find colormap: error %ld\n", PTR_ERR(cmap_base));
		} else {
			odev->cmap_base = cmap_base;
		}
	}

	/* EDID is optional */
	edid = display_get_edid_of(dev, of_node, odev->edid);

	/*
	 * Firmware framebuffer
	 */

	iosys_map_set_vaddr_iomem(&sysfb->fb_addr, screen_base);
	sysfb->fb_mode = drm_sysfb_mode(width, height, 0, 0);
	sysfb->fb_format = format;
	sysfb->fb_pitch = linebytes;
	if (odev->cmap_base)
		sysfb->fb_gamma_lut_size = OFDRM_GAMMA_LUT_SIZE;
	sysfb->edid = edid;

	drm_dbg(dev, "display mode={" DRM_MODE_FMT "}\n", DRM_MODE_ARG(&sysfb->fb_mode));
	drm_dbg(dev, "framebuffer format=%p4cc, size=%dx%d, linebytes=%d byte\n",
		&format->format, width, height, linebytes);

	/*
	 * Mode-setting pipeline
	 */

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ERR_PTR(ret);

	max_width = max_t(unsigned long, width, DRM_SHADOW_PLANE_MAX_WIDTH);
	max_height = max_t(unsigned long, height, DRM_SHADOW_PLANE_MAX_HEIGHT);

	dev->mode_config.min_width = width;
	dev->mode_config.max_width = max_width;
	dev->mode_config.min_height = height;
	dev->mode_config.max_height = max_height;
	dev->mode_config.funcs = &ofdrm_mode_config_funcs;
	dev->mode_config.preferred_depth = format->depth;
	dev->mode_config.quirk_addfb_prefer_host_byte_order = true;

	/* Primary plane */

	nformats = drm_sysfb_build_fourcc_list(dev, &format->format, 1,
					       odev->formats, ARRAY_SIZE(odev->formats));

	primary_plane = &odev->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0, &ofdrm_primary_plane_funcs,
				       odev->formats, nformats,
				       ofdrm_primary_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_plane_helper_add(primary_plane, &ofdrm_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	/* CRTC */

	crtc = &odev->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&ofdrm_crtc_funcs, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_crtc_helper_add(crtc, &ofdrm_crtc_helper_funcs);

	if (sysfb->fb_gamma_lut_size) {
		ret = drm_mode_crtc_set_gamma_size(crtc, sysfb->fb_gamma_lut_size);
		if (!ret)
			drm_crtc_enable_color_mgmt(crtc, 0, false, sysfb->fb_gamma_lut_size);
	}

	/* Encoder */

	encoder = &odev->encoder;
	ret = drm_encoder_init(dev, encoder, &ofdrm_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ERR_PTR(ret);
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* Connector */

	connector = &odev->connector;
	ret = drm_connector_init(dev, connector, &ofdrm_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ERR_PTR(ret);
	drm_connector_helper_add(connector, &ofdrm_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       width, height);
	if (edid)
		drm_connector_attach_edid_property(connector);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);

	return odev;
}

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(ofdrm_fops);

static struct drm_driver ofdrm_driver = {
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.driver_features	= DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops			= &ofdrm_fops,
};

/*
 * Platform driver
 */

static int ofdrm_probe(struct platform_device *pdev)
{
	struct ofdrm_device *odev;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	int ret;

	odev = ofdrm_device_create(&ofdrm_driver, pdev);
	if (IS_ERR(odev))
		return PTR_ERR(odev);
	sysfb = &odev->sysfb;
	dev = &sysfb->dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_client_setup(dev, sysfb->fb_format);

	return 0;
}

static void ofdrm_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);

	drm_dev_unplug(dev);
}

static const struct of_device_id ofdrm_of_match_display[] = {
	{ .compatible = "display", },
	{ },
};
MODULE_DEVICE_TABLE(of, ofdrm_of_match_display);

static struct platform_driver ofdrm_platform_driver = {
	.driver = {
		.name = "of-display",
		.of_match_table = ofdrm_of_match_display,
	},
	.probe = ofdrm_probe,
	.remove = ofdrm_remove,
};

module_platform_driver(ofdrm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
