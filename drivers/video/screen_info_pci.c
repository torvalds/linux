// SPDX-License-Identifier: GPL-2.0

#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/screen_info.h>
#include <linux/string.h>

static struct pci_dev *screen_info_lfb_pdev;
static size_t screen_info_lfb_bar;
static resource_size_t screen_info_lfb_offset;
static struct resource screen_info_lfb_res = DEFINE_RES_MEM(0, 0);

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

		if (pr->start != screen_info_lfb_res.start) {
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

static void screen_info_fixup_lfb(struct pci_dev *pdev)
{
	unsigned int type;
	struct resource res[SCREEN_INFO_MAX_RESOURCES];
	size_t i, numres;
	int ret;
	const struct screen_info *si = &screen_info;

	if (screen_info_lfb_pdev)
		return; // already found

	type = screen_info_video_type(si);
	if (type != VIDEO_TYPE_EFI)
		return; // only applies to EFI

	ret = screen_info_resources(si, res, ARRAY_SIZE(res));
	if (ret < 0)
		return;
	numres = ret;

	for (i = 0; i < numres; ++i) {
		struct resource *r = &res[i];
		const struct resource *pr;

		if (!(r->flags & IORESOURCE_MEM))
			continue;
		pr = pci_find_resource(pdev, r);
		if (!pr)
			continue;

		/*
		 * We've found a PCI device with the framebuffer
		 * resource. Store away the parameters to track
		 * relocation of the framebuffer aperture.
		 */
		screen_info_lfb_pdev = pdev;
		screen_info_lfb_bar = pr - pdev->resource;
		screen_info_lfb_offset = r->start - pr->start;
		memcpy(&screen_info_lfb_res, r, sizeof(screen_info_lfb_res));
	}
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
