// SPDX-License-Identifier: GPL-2.0

#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/screen_info.h>
#include <linux/string.h>

static struct pci_dev *screen_info_lfb_pdev;
static size_t screen_info_lfb_bar;
static resource_size_t screen_info_lfb_res_start; // original start of resource
static resource_size_t screen_info_lfb_offset; // framebuffer offset within resource

static bool __screen_info_relocation_is_valid(const struct screen_info *si, struct resource *pr)
{
	u64 size = __screen_info_lfb_size(si, screen_info_video_type(si));

	if (screen_info_lfb_offset > resource_size(pr))
		return false;
	if (size > resource_size(pr))
		return false;
	if (resource_size(pr) - size < screen_info_lfb_offset)
		return false;

	return true;
}

void screen_info_apply_fixups(void)
{
	struct screen_info *si = &screen_info;

	if (screen_info_lfb_pdev) {
		struct resource *pr = &screen_info_lfb_pdev->resource[screen_info_lfb_bar];

		if (pr->start != screen_info_lfb_res_start) {
			if (__screen_info_relocation_is_valid(si, pr)) {
				/*
				 * Only update base if we have an actual
				 * relocation to a valid I/O range.
				 */
				__screen_info_set_lfb_base(si, pr->start + screen_info_lfb_offset);
				pr_info("Relocating firmware framebuffer to offset %pa[d] within %pr\n",
					&screen_info_lfb_offset, pr);
			} else {
				pr_warn("Invalid relocating, disabling firmware framebuffer\n");
			}
		}
	}
}

static int __screen_info_lfb_pci_bus_region(const struct screen_info *si, unsigned int type,
					    struct pci_bus_region *r)
{
	u64 base, size;

	base = __screen_info_lfb_base(si);
	if (!base)
		return -EINVAL;

	size = __screen_info_lfb_size(si, type);
	if (!size)
		return -EINVAL;

	r->start = base;
	r->end = base + size - 1;

	return 0;
}

static void screen_info_fixup_lfb(struct pci_dev *pdev)
{
	unsigned int type;
	struct pci_bus_region bus_region;
	int ret;
	struct resource r = {
		.flags = IORESOURCE_MEM,
	};
	const struct resource *pr;
	const struct screen_info *si = &screen_info;

	if (screen_info_lfb_pdev)
		return; // already found

	type = screen_info_video_type(si);
	if (!__screen_info_has_lfb(type))
		return; // only applies to EFI; maybe VESA

	ret = __screen_info_lfb_pci_bus_region(si, type, &bus_region);
	if (ret < 0)
		return;

	/*
	 * Translate the PCI bus address to resource. Account
	 * for an offset if the framebuffer is behind a PCI host
	 * bridge.
	 */
	pcibios_bus_to_resource(pdev->bus, &r, &bus_region);

	pr = pci_find_resource(pdev, &r);
	if (!pr)
		return;

	/*
	 * We've found a PCI device with the framebuffer
	 * resource. Store away the parameters to track
	 * relocation of the framebuffer aperture.
	 */
	screen_info_lfb_pdev = pdev;
	screen_info_lfb_bar = pr - pdev->resource;
	screen_info_lfb_offset = r.start - pr->start;
	screen_info_lfb_res_start = bus_region.start;
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY, 16,
			       screen_info_fixup_lfb);

static struct pci_dev *__screen_info_pci_dev(struct resource *res)
{
	struct pci_dev *pdev = NULL;
	const struct resource *r = NULL;

	if (!(res->flags & IORESOURCE_MEM))
		return NULL;

	while (!r && (pdev = pci_get_base_class(PCI_BASE_CLASS_DISPLAY, pdev))) {
		r = pci_find_resource(pdev, res);
	}

	return pdev;
}

/**
 * screen_info_pci_dev() - Return PCI parent device that contains screen_info's framebuffer
 * @si: the screen_info
 *
 * Returns:
 * The screen_info's parent device or NULL on success, or a pointer-encoded
 * errno value otherwise. The value NULL is not an error. It signals that no
 * PCI device has been found.
 */
struct pci_dev *screen_info_pci_dev(const struct screen_info *si)
{
	struct resource res[SCREEN_INFO_MAX_RESOURCES];
	ssize_t i, numres;

	numres = screen_info_resources(si, res, ARRAY_SIZE(res));
	if (numres < 0)
		return ERR_PTR(numres);

	for (i = 0; i < numres; ++i) {
		struct pci_dev *pdev = __screen_info_pci_dev(&res[i]);

		if (pdev)
			return pdev;
	}

	return NULL;
}
EXPORT_SYMBOL(screen_info_pci_dev);
