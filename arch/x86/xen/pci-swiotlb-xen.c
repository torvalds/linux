// SPDX-License-Identifier: GPL-2.0

/* Glue code to lib/swiotlb-xen.c */

#include <linux/dma-map-ops.h>
#include <linux/pci.h>
#include <xen/swiotlb-xen.h>

#include <asm/xen/hypervisor.h>
#include <xen/xen.h>
#include <asm/iommu_table.h>


#include <asm/xen/swiotlb-xen.h>
#ifdef CONFIG_X86_64
#include <asm/iommu.h>
#include <asm/dma.h>
#endif
#include <linux/export.h>

static int xen_swiotlb __read_mostly;

/*
 * pci_xen_swiotlb_detect - set xen_swiotlb to 1 if necessary
 *
 * This returns non-zero if we are forced to use xen_swiotlb (by the boot
 * option).
 */
int __init pci_xen_swiotlb_detect(void)
{

	if (!xen_pv_domain())
		return 0;

	/* If running as PV guest, either iommu=soft, or swiotlb=force will
	 * activate this IOMMU. If running as PV privileged, activate it
	 * irregardless.
	 */
	if (xen_initial_domain() || swiotlb || swiotlb_force == SWIOTLB_FORCE)
		xen_swiotlb = 1;

	/* If we are running under Xen, we MUST disable the native SWIOTLB.
	 * Don't worry about swiotlb_force flag activating the native, as
	 * the 'swiotlb' flag is the only one turning it on. */
	swiotlb = 0;

#ifdef CONFIG_X86_64
	/* pci_swiotlb_detect_4gb turns on native SWIOTLB if no_iommu == 0
	 * (so no iommu=X command line over-writes).
	 * Considering that PV guests do not want the *native SWIOTLB* but
	 * only Xen SWIOTLB it is not useful to us so set no_iommu=1 here.
	 */
	if (max_pfn > MAX_DMA32_PFN)
		no_iommu = 1;
#endif
	return xen_swiotlb;
}

static void __init pci_xen_swiotlb_init(void)
{
	if (xen_swiotlb) {
		xen_swiotlb_init_early();
		dma_ops = &xen_swiotlb_dma_ops;

#ifdef CONFIG_PCI
		/* Make sure ACS will be enabled */
		pci_request_acs();
#endif
	}
}

int pci_xen_swiotlb_init_late(void)
{
	int rc;

	if (xen_swiotlb)
		return 0;

	rc = xen_swiotlb_init();
	if (rc)
		return rc;

	dma_ops = &xen_swiotlb_dma_ops;
#ifdef CONFIG_PCI
	/* Make sure ACS will be enabled */
	pci_request_acs();
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(pci_xen_swiotlb_init_late);

IOMMU_INIT_FINISH(pci_xen_swiotlb_detect,
		  NULL,
		  pci_xen_swiotlb_init,
		  NULL);
