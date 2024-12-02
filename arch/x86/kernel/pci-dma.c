// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/dmar.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/amd-iommu.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/x86_init.h>

#include <xen/xen.h>
#include <xen/swiotlb-xen.h>

static bool disable_dac_quirk __read_mostly;

const struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __read_mostly = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly = 0;
#endif

int iommu_merge __read_mostly = 0;

int no_iommu __read_mostly;
/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly = 0;

#ifdef CONFIG_SWIOTLB
bool x86_swiotlb_enable;
static unsigned int x86_swiotlb_flags;

static void __init pci_swiotlb_detect(void)
{
	/* don't initialize swiotlb if iommu=off (no_iommu=1) */
	if (!no_iommu && max_possible_pfn > MAX_DMA32_PFN)
		x86_swiotlb_enable = true;

	/*
	 * Set swiotlb to 1 so that bounce buffers are allocated and used for
	 * devices that can't support DMA to encrypted memory.
	 */
	if (cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		x86_swiotlb_enable = true;

	/*
	 * Guest with guest memory encryption currently perform all DMA through
	 * bounce buffers as the hypervisor can't access arbitrary VM memory
	 * that is not explicitly shared with it.
	 */
	if (cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT)) {
		x86_swiotlb_enable = true;
		x86_swiotlb_flags |= SWIOTLB_FORCE;
	}
}
#else
static inline void __init pci_swiotlb_detect(void)
{
}
#define x86_swiotlb_flags 0
#endif /* CONFIG_SWIOTLB */

#ifdef CONFIG_SWIOTLB_XEN
static bool xen_swiotlb_enabled(void)
{
	return xen_initial_domain() || x86_swiotlb_enable ||
		(IS_ENABLED(CONFIG_XEN_PCIDEV_FRONTEND) && xen_pv_pci_possible);
}

static void __init pci_xen_swiotlb_init(void)
{
	if (!xen_swiotlb_enabled())
		return;
	x86_swiotlb_enable = true;
	x86_swiotlb_flags |= SWIOTLB_ANY;
	swiotlb_init_remap(true, x86_swiotlb_flags, xen_swiotlb_fixup);
	dma_ops = &xen_swiotlb_dma_ops;
	if (IS_ENABLED(CONFIG_PCI))
		pci_request_acs();
}
#else
static inline void __init pci_xen_swiotlb_init(void)
{
}
#endif /* CONFIG_SWIOTLB_XEN */

void __init pci_iommu_alloc(void)
{
	if (xen_pv_domain()) {
		pci_xen_swiotlb_init();
		return;
	}
	pci_swiotlb_detect();
	gart_iommu_hole_init();
	amd_iommu_detect();
	detect_intel_iommu();
	swiotlb_init(x86_swiotlb_enable, x86_swiotlb_flags);
}

static __init int iommu_setup(char *p)
{
	iommu_merge = 1;

	if (!p)
		return -EINVAL;

	while (*p) {
		if (!strncmp(p, "off", 3))
			no_iommu = 1;
		/* gart_parse_options has more force support */
		if (!strncmp(p, "force", 5))
			force_iommu = 1;
		if (!strncmp(p, "noforce", 7)) {
			iommu_merge = 0;
			force_iommu = 0;
		}

		if (!strncmp(p, "biomerge", 8)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "panic", 5))
			panic_on_overflow = 1;
		if (!strncmp(p, "nopanic", 7))
			panic_on_overflow = 0;
		if (!strncmp(p, "merge", 5)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "nomerge", 7))
			iommu_merge = 0;
		if (!strncmp(p, "forcesac", 8))
			pr_warn("forcesac option ignored.\n");
		if (!strncmp(p, "allowdac", 8))
			pr_warn("allowdac option ignored.\n");
		if (!strncmp(p, "nodac", 5))
			pr_warn("nodac option ignored.\n");
		if (!strncmp(p, "usedac", 6)) {
			disable_dac_quirk = true;
			return 1;
		}
#ifdef CONFIG_SWIOTLB
		if (!strncmp(p, "soft", 4))
			x86_swiotlb_enable = true;
#endif
		if (!strncmp(p, "pt", 2))
			iommu_set_default_passthrough(true);
		if (!strncmp(p, "nopt", 4))
			iommu_set_default_translated(true);

		gart_parse_options(p);

		p += strcspn(p, ",");
		if (*p == ',')
			++p;
	}
	return 0;
}
early_param("iommu", iommu_setup);

static int __init pci_iommu_init(void)
{
	x86_init.iommu.iommu_init();

#ifdef CONFIG_SWIOTLB
	/* An IOMMU turned us off. */
	if (x86_swiotlb_enable) {
		pr_info("PCI-DMA: Using software bounce buffering for IO (SWIOTLB)\n");
		swiotlb_print_info();
	} else {
		swiotlb_exit();
	}
#endif

	return 0;
}
/* Must execute after PCI subsystem */
rootfs_initcall(pci_iommu_init);

#ifdef CONFIG_PCI
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static int via_no_dac_cb(struct pci_dev *pdev, void *data)
{
	pdev->dev.bus_dma_limit = DMA_BIT_MASK(32);
	return 0;
}

static void via_no_dac(struct pci_dev *dev)
{
	if (!disable_dac_quirk) {
		dev_info(&dev->dev, "disabling DAC on VIA PCI bridge\n");
		pci_walk_bus(dev->subordinate, via_no_dac_cb, NULL);
	}
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID,
				PCI_CLASS_BRIDGE_PCI, 8, via_no_dac);
#endif
