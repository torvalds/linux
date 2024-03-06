// SPDX-License-Identifier: GPL-2.0-only

#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define DRIVER_NAME	"ofdrm"
#define DRIVER_DESC	"DRM driver for OF platform devices"
#define DRIVER_DATE	"20220501"
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
	if (value > INT_MAX) {
		drm_err(dev, "invalid framebuffer %s of %u\n", name, value);
		return -EINVAL;
	}
	return (int)value;
}

static int display_get_validated_int0(struct drm_device *dev, const char *name, uint32_t value)
{
	if (!value) {
		drm_err(dev, "invalid framebuffer %s of %u\n", name, value);
		return -EINVAL;
	}
	return display_get_validated_int(dev, name, value);
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
	struct drm_device dev;
	struct platform_device *pdev;

	const struct ofdrm_device_funcs *funcs;

	/* firmware-buffer settings */
	struct iosys_map screen_base;
	struct drm_display_mode mode;
	const struct drm_format_info *format;
	unsigned int pitch;

	/* colormap */
	void __iomem *cmap_base;

	/* modesetting */
	uint32_t formats[8];
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct ofdrm_device *ofdrm_device_of_dev(struct drm_device *dev)
{
	return container_of(dev, struct ofdrm_device, dev);
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
	struct drm_device *dev = &odev->dev;
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
	struct platform_device *pdev = to_platform_device(odev->dev.dev);
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
	struct drm_device *dev = &odev->dev;
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
	struct drm_device *dev = &odev->dev;
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

	struct drm_device *dev = &odev->dev;
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

static void ofdrm_device_set_gamma_linear(struct ofdrm_device *odev,
					  const struct drm_format_info *format)
{
	struct drm_device *dev = &odev->dev;
	int i;

	switch (format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB565 | DRM_FORMAT_BIG_ENDIAN:
		/* Use better interpolation, to take 32 values from 0 to 255 */
		for (i = 0; i < OFDRM_GAMMA_LUT_SIZE / 8; i++) {
			unsigned char r = i * 8 + i / 4;
			unsigned char g = i * 4 + i / 16;
			unsigned char b = i * 8 + i / 4;

			odev->funcs->cmap_write(odev, i, r, g, b);
		}
		/* Green has one more bit, so add padding with 0 for red and blue. */
		for (i = OFDRM_GAMMA_LUT_SIZE / 8; i < OFDRM_GAMMA_LUT_SIZE / 4; i++) {
			unsigned char r = 0;
			unsigned char g = i * 4 + i / 16;
			unsigned char b = 0;

			odev->funcs->cmap_write(odev, i, r, g, b);
		}
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGRX8888:
		for (i = 0; i < OFDRM_GAMMA_LUT_SIZE; i++)
			odev->funcs->cmap_write(odev, i, i, i, i);
		break;
	default:
		drm_warn_once(dev, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

static void ofdrm_device_set_gamma(struct ofdrm_device *odev,
				   const struct drm_format_info *format,
				   struct drm_color_lut *lut)
{
	struct drm_device *dev = &odev->dev;
	int i;

	switch (format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB565 | DRM_FORMAT_BIG_ENDIAN:
		/* Use better interpolation, to take 32 values from lut[0] to lut[255] */
		for (i = 0; i < OFDRM_GAMMA_LUT_SIZE / 8; i++) {
			unsigned char r = lut[i * 8 + i / 4].red >> 8;
			unsigned char g = lut[i * 4 + i / 16].green >> 8;
			unsigned char b = lut[i * 8 + i / 4].blue >> 8;

			odev->funcs->cmap_write(odev, i, r, g, b);
		}
		/* Green has one more bit, so add padding with 0 for red and blue. */
		for (i = OFDRM_GAMMA_LUT_SIZE / 8; i < OFDRM_GAMMA_LUT_SIZE / 4; i++) {
			unsigned char r = 0;
			unsigned char g = lut[i * 4 + i / 16].green >> 8;
			unsigned char b = 0;

			odev->funcs->cmap_write(odev, i, r, g, b);
		}
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGRX8888:
		for (i = 0; i < OFDRM_GAMMA_LUT_SIZE; i++) {
			unsigned char r = lut[i].red >> 8;
			unsigned char g = lut[i].green >> 8;
			unsigned char b = lut[i].blue >> 8;

			odev->funcs->cmap_write(odev, i, r, g, b);
		}
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

struct ofdrm_crtc_state {
	struct drm_crtc_state base;

	/* Primary-plane format; required for color mgmt. */
	const struct drm_format_info *format;
};

static struct ofdrm_crtc_state *to_ofdrm_crtc_state(struct drm_crtc_state *base)
{
	return container_of(base, struct ofdrm_crtc_state, base);
}

static void ofdrm_crtc_state_destroy(struct ofdrm_crtc_state *ofdrm_crtc_state)
{
	__drm_atomic_helper_crtc_destroy_state(&ofdrm_crtc_state->base);
	kfree(ofdrm_crtc_state);
}

static const uint64_t ofdrm_primary_plane_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static int ofdrm_primary_plane_helper_atomic_check(struct drm_plane *plane,
						   struct drm_atomic_state *new_state)
{
	struct drm_device *dev = plane->dev;
	struct ofdrm_device *odev = ofdrm_device_of_dev(dev);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(new_state, plane);
	struct drm_shadow_plane_state *new_shadow_plane_state =
		to_drm_shadow_plane_state(new_plane_state);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_crtc *new_crtc = new_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state = NULL;
	struct ofdrm_crtc_state *new_ofdrm_crtc_state;
	int ret;

	if (new_crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(new_state, new_plane_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;
	else if (!new_plane_state->visible)
		return 0;

	if (new_fb->format != odev->format) {
		void *buf;

		/* format conversion necessary; reserve buffer */
		buf = drm_format_conv_state_reserve(&new_shadow_plane_state->fmtcnv_state,
						    odev->pitch, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	new_crtc_state = drm_atomic_get_new_crtc_state(new_state, new_plane_state->crtc);

	new_ofdrm_crtc_state = to_ofdrm_crtc_state(new_crtc_state);
	new_ofdrm_crtc_state->format = new_fb->format;

	return 0;
}

static void ofdrm_primary_plane_helper_atomic_update(struct drm_plane *plane,
						     struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct ofdrm_device *odev = ofdrm_device_of_dev(dev);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	unsigned int dst_pitch = odev->pitch;
	const struct drm_format_info *dst_format = odev->format;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;
	int ret, idx;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return;

	if (!drm_dev_enter(dev, &idx))
		goto out_drm_gem_fb_end_cpu_access;

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		struct iosys_map dst = odev->screen_base;
		struct drm_rect dst_clip = plane_state->dst;

		if (!drm_rect_intersect(&dst_clip, &damage))
			continue;

		iosys_map_incr(&dst, drm_fb_clip_offset(dst_pitch, dst_format, &dst_clip));
		drm_fb_blit(&dst, &dst_pitch, dst_format->format, shadow_plane_state->data, fb,
			    &damage, &shadow_plane_state->fmtcnv_state);
	}

	drm_dev_exit(idx);
out_drm_gem_fb_end_cpu_access:
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
}

static void ofdrm_primary_plane_helper_atomic_disable(struct drm_plane *plane,
						      struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct ofdrm_device *odev = ofdrm_device_of_dev(dev);
	struct iosys_map dst = odev->screen_base;
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	void __iomem *dst_vmap = dst.vaddr_iomem; /* TODO: Use mapping abstraction */
	unsigned int dst_pitch = odev->pitch;
	const struct drm_format_info *dst_format = odev->format;
	struct drm_rect dst_clip;
	unsigned long lines, linepixels, i;
	int idx;

	drm_rect_init(&dst_clip,
		      plane_state->src_x >> 16, plane_state->src_y >> 16,
		      plane_state->src_w >> 16, plane_state->src_h >> 16);

	lines = drm_rect_height(&dst_clip);
	linepixels = drm_rect_width(&dst_clip);

	if (!drm_dev_enter(dev, &idx))
		return;

	/* Clear buffer to black if disabled */
	dst_vmap += drm_fb_clip_offset(dst_pitch, dst_format, &dst_clip);
	for (i = 0; i < lines; ++i) {
		memset_io(dst_vmap, 0, linepixels * dst_format->cpp[0]);
		dst_vmap += dst_pitch;
	}

	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs ofdrm_primary_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ofdrm_primary_plane_helper_atomic_check,
	.atomic_update = ofdrm_primary_plane_helper_atomic_update,
	.atomic_disable = ofdrm_primary_plane_helper_atomic_disable,
};

static const struct drm_plane_funcs ofdrm_primary_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static enum drm_mode_status ofdrm_crtc_helper_mode_valid(struct drm_crtc *crtc,
							 const struct drm_display_mode *mode)
{
	struct ofdrm_device *odev = ofdrm_device_of_dev(crtc->dev);

	return drm_crtc_helper_mode_valid_fixed(crtc, mode, &odev->mode);
}

static int ofdrm_crtc_helper_atomic_check(struct drm_crtc *crtc,
					  struct drm_atomic_state *new_state)
{
	static const size_t gamma_lut_length = OFDRM_GAMMA_LUT_SIZE * sizeof(struct drm_color_lut);

	struct drm_device *dev = crtc->dev;
	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	int ret;

	if (!new_crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_primary_plane(new_crtc_state);
	if (ret)
		return ret;

	if (new_crtc_state->color_mgmt_changed) {
		struct drm_property_blob *gamma_lut = new_crtc_state->gamma_lut;

		if (gamma_lut && (gamma_lut->length != gamma_lut_length)) {
			drm_dbg(dev, "Incorrect gamma_lut length %zu\n", gamma_lut->length);
			return -EINVAL;
		}
	}

	return 0;
}

static void ofdrm_crtc_helper_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct ofdrm_device *odev = ofdrm_device_of_dev(crtc->dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct ofdrm_crtc_state *ofdrm_crtc_state = to_ofdrm_crtc_state(crtc_state);

	if (crtc_state->enable && crtc_state->color_mgmt_changed) {
		const struct drm_format_info *format = ofdrm_crtc_state->format;

		if (crtc_state->gamma_lut)
			ofdrm_device_set_gamma(odev, format, crtc_state->gamma_lut->data);
		else
			ofdrm_device_set_gamma_linear(odev, format);
	}
}

/*
 * The CRTC is always enabled. Screen updates are performed by
 * the primary plane's atomic_update function. Disabling clears
 * the screen in the primary plane's atomic_disable function.
 */
static const struct drm_crtc_helper_funcs ofdrm_crtc_helper_funcs = {
	.mode_valid = ofdrm_crtc_helper_mode_valid,
	.atomic_check = ofdrm_crtc_helper_atomic_check,
	.atomic_flush = ofdrm_crtc_helper_atomic_flush,
};

static void ofdrm_crtc_reset(struct drm_crtc *crtc)
{
	struct ofdrm_crtc_state *ofdrm_crtc_state =
		kzalloc(sizeof(*ofdrm_crtc_state), GFP_KERNEL);

	if (crtc->state)
		ofdrm_crtc_state_destroy(to_ofdrm_crtc_state(crtc->state));

	if (ofdrm_crtc_state)
		__drm_atomic_helper_crtc_reset(crtc, &ofdrm_crtc_state->base);
	else
		__drm_atomic_helper_crtc_reset(crtc, NULL);
}

static struct drm_crtc_state *ofdrm_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct ofdrm_crtc_state *new_ofdrm_crtc_state;
	struct ofdrm_crtc_state *ofdrm_crtc_state;

	if (drm_WARN_ON(dev, !crtc_state))
		return NULL;

	new_ofdrm_crtc_state = kzalloc(sizeof(*new_ofdrm_crtc_state), GFP_KERNEL);
	if (!new_ofdrm_crtc_state)
		return NULL;

	ofdrm_crtc_state = to_ofdrm_crtc_state(crtc_state);

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_ofdrm_crtc_state->base);
	new_ofdrm_crtc_state->format = ofdrm_crtc_state->format;

	return &new_ofdrm_crtc_state->base;
}

static void ofdrm_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					    struct drm_crtc_state *crtc_state)
{
	ofdrm_crtc_state_destroy(to_ofdrm_crtc_state(crtc_state));
}

static const struct drm_crtc_funcs ofdrm_crtc_funcs = {
	.reset = ofdrm_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = ofdrm_crtc_atomic_duplicate_state,
	.atomic_destroy_state = ofdrm_crtc_atomic_destroy_state,
};

static int ofdrm_connector_helper_get_modes(struct drm_connector *connector)
{
	struct ofdrm_device *odev = ofdrm_device_of_dev(connector->dev);

	return drm_connector_helper_get_modes_fixed(connector, &odev->mode);
}

static const struct drm_connector_helper_funcs ofdrm_connector_helper_funcs = {
	.get_modes = ofdrm_connector_helper_get_modes,
};

static const struct drm_connector_funcs ofdrm_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs ofdrm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
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

static struct drm_display_mode ofdrm_mode(unsigned int width, unsigned int height)
{
	/*
	 * Assume a monitor resolution of 96 dpi to
	 * get a somewhat reasonable screen size.
	 */
	const struct drm_display_mode mode = {
		DRM_MODE_INIT(60, width, height,
			      DRM_MODE_RES_MM(width, 96ul),
			      DRM_MODE_RES_MM(height, 96ul))
	};

	return mode;
}

static struct ofdrm_device *ofdrm_device_create(struct drm_driver *drv,
						struct platform_device *pdev)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct ofdrm_device *odev;
	struct drm_device *dev;
	enum ofdrm_model model;
	bool big_endian;
	int width, height, depth, linebytes;
	const struct drm_format_info *format;
	u64 address;
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

	odev = devm_drm_dev_alloc(&pdev->dev, drv, struct ofdrm_device, dev);
	if (IS_ERR(odev))
		return ERR_CAST(odev);
	dev = &odev->dev;
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

	ret = devm_aperture_acquire_from_firmware(dev, fb_pgbase, fb_pgsize);
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

	/*
	 * Firmware framebuffer
	 */

	iosys_map_set_vaddr_iomem(&odev->screen_base, screen_base);
	odev->mode = ofdrm_mode(width, height);
	odev->format = format;
	odev->pitch = linebytes;

	drm_dbg(dev, "display mode={" DRM_MODE_FMT "}\n", DRM_MODE_ARG(&odev->mode));
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

	nformats = drm_fb_build_fourcc_list(dev, &format->format, 1,
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

	if (odev->cmap_base) {
		drm_mode_crtc_set_gamma_size(crtc, OFDRM_GAMMA_LUT_SIZE);
		drm_crtc_enable_color_mgmt(crtc, 0, false, OFDRM_GAMMA_LUT_SIZE);
	}

	/* Encoder */

	encoder = &odev->encoder;
	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_NONE);
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
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
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
	struct drm_device *dev;
	unsigned int color_mode;
	int ret;

	odev = ofdrm_device_create(&ofdrm_driver, pdev);
	if (IS_ERR(odev))
		return PTR_ERR(odev);
	dev = &odev->dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	color_mode = drm_format_info_bpp(odev->format, 0);
	if (color_mode == 16)
		color_mode = odev->format->depth; // can be 15 or 16

	drm_fbdev_generic_setup(dev, color_mode);

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
	.remove_new = ofdrm_remove,
};

module_platform_driver(ofdrm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
