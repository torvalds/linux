// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/bitmap.h>
#include <linux/pci.h>

#include <asm/opal.h>

#include "pci.h"

/*
 * The majority of the complexity in supporting SR-IOV on PowerNV comes from
 * the need to put the MMIO space for each VF into a separate PE. Internally
 * the PHB maps MMIO addresses to a specific PE using the "Memory BAR Table".
 * The MBT historically only applied to the 64bit MMIO window of the PHB
 * so it's common to see it referred to as the "M64BT".
 *
 * An MBT entry stores the mapped range as an <base>,<mask> pair. This forces
 * the address range that we want to map to be power-of-two sized and aligned.
 * For conventional PCI devices this isn't really an issue since PCI device BARs
 * have the same requirement.
 *
 * For a SR-IOV BAR things are a little more awkward since size and alignment
 * are not coupled. The alignment is set based on the per-VF BAR size, but
 * the total BAR area is: number-of-vfs * per-vf-size. The number of VFs
 * isn't necessarily a power of two, so neither is the total size. To fix that
 * we need to finesse (read: hack) the Linux BAR allocator so that it will
 * allocate the SR-IOV BARs in a way that lets us map them using the MBT.
 *
 * The changes to size and alignment that we need to do depend on the "mode"
 * of MBT entry that we use. We only support SR-IOV on PHB3 (IODA2) and above,
 * so as a baseline we can assume that we have the following BAR modes
 * available:
 *
 *   NB: $PE_COUNT is the number of PEs that the PHB supports.
 *
 * a) A segmented BAR that splits the mapped range into $PE_COUNT equally sized
 *    segments. The n'th segment is mapped to the n'th PE.
 * b) An un-segmented BAR that maps the whole address range to a specific PE.
 *
 *
 * We prefer to use mode a) since it only requires one MBT entry per SR-IOV BAR
 * For comparison b) requires one entry per-VF per-BAR, or:
 * (num-vfs * num-sriov-bars) in total. To use a) we need the size of each segment
 * to equal the size of the per-VF BAR area. So:
 *
 *	new_size = per-vf-size * number-of-PEs
 *
 * The alignment for the SR-IOV BAR also needs to be changed from per-vf-size
 * to "new_size", calculated above. Implementing this is a convoluted process
 * which requires several hooks in the PCI core:
 *
 * 1. In pcibios_device_add() we call pnv_pci_ioda_fixup_iov().
 *
 *    At this point the device has been probed and the device's BARs are sized,
 *    but no resource allocations have been done. The SR-IOV BARs are sized
 *    based on the maximum number of VFs supported by the device and we need
 *    to increase that to new_size.
 *
 * 2. Later, when Linux actually assigns resources it tries to make the resource
 *    allocations for each PCI bus as compact as possible. As a part of that it
 *    sorts the BARs on a bus by their required alignment, which is calculated
 *    using pci_resource_alignment().
 *
 *    For IOV resources this goes:
 *    pci_resource_alignment()
 *        pci_sriov_resource_alignment()
 *            pcibios_sriov_resource_alignment()
 *                pnv_pci_iov_resource_alignment()
 *
 *    Our hook overrides the default alignment, equal to the per-vf-size, with
 *    new_size computed above.
 *
 * 3. When userspace enables VFs for a device:
 *
 *    sriov_enable()
 *       pcibios_sriov_enable()
 *           pnv_pcibios_sriov_enable()
 *
 *    This is where we actually allocate PE numbers for each VF and setup the
 *    MBT mapping for each SR-IOV BAR. In steps 1) and 2) we setup an "arena"
 *    where each MBT segment is equal in size to the VF BAR so we can shift
 *    around the actual SR-IOV BAR location within this arena. We need this
 *    ability because the PE space is shared by all devices on the same PHB.
 *    When using mode a) described above segment 0 in maps to PE#0 which might
 *    be already being used by another device on the PHB.
 *
 *    As a result we need allocate a contigious range of PE numbers, then shift
 *    the address programmed into the SR-IOV BAR of the PF so that the address
 *    of VF0 matches up with the segment corresponding to the first allocated
 *    PE number. This is handled in pnv_pci_vf_resource_shift().
 *
 *    Once all that is done we return to the PCI core which then enables VFs,
 *    scans them and creates pci_devs for each. The init process for a VF is
 *    largely the same as a normal device, but the VF is inserted into the IODA
 *    PE that we allocated for it rather than the PE associated with the bus.
 *
 * 4. When userspace disables VFs we unwind the above in
 *    pnv_pcibios_sriov_disable(). Fortunately this is relatively simple since
 *    we don't need to validate anything, just tear down the mappings and
 *    move SR-IOV resource back to its "proper" location.
 *
 * That's how mode a) works. In theory mode b) (single PE mapping) is less work
 * since we can map each individual VF with a separate BAR. However, there's a
 * few limitations:
 *
 * 1) For IODA2 mode b) has a minimum alignment requirement of 32MB. This makes
 *    it only usable for devices with very large per-VF BARs. Such devices are
 *    similar to Big Foot. They definitely exist, but I've never seen one.
 *
 * 2) The number of MBT entries that we have is limited. PHB3 and PHB4 only
 *    16 total and some are needed for. Most SR-IOV capable network cards can support
 *    more than 16 VFs on each port.
 *
 * We use b) when using a) would use more than 1/4 of the entire 64 bit MMIO
 * window of the PHB.
 *
 *
 *
 * PHB4 (IODA3) added a few new features that would be useful for SR-IOV. It
 * allowed the MBT to map 32bit MMIO space in addition to 64bit which allows
 * us to support SR-IOV BARs in the 32bit MMIO window. This is useful since
 * the Linux BAR allocation will place any BAR marked as non-prefetchable into
 * the non-prefetchable bridge window, which is 32bit only. It also added two
 * new modes:
 *
 * c) A segmented BAR similar to a), but each segment can be individually
 *    mapped to any PE. This is matches how the 32bit MMIO window worked on
 *    IODA1&2.
 *
 * d) A segmented BAR with 8, 64, or 128 segments. This works similarly to a),
 *    but with fewer segments and configurable base PE.
 *
 *    i.e. The n'th segment maps to the (n + base)'th PE.
 *
 *    The base PE is also required to be a multiple of the window size.
 *
 * Unfortunately, the OPAL API doesn't currently (as of skiboot v6.6) allow us
 * to exploit any of the IODA3 features.
 */

static void pnv_pci_ioda_fixup_iov_resources(struct pci_dev *pdev)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct resource *res;
	int i;
	resource_size_t vf_bar_sz;
	struct pnv_iov_data *iov;
	int mul;

	iov = kzalloc(sizeof(*iov), GFP_KERNEL);
	if (!iov)
		goto disable_iov;
	pdev->dev.archdata.iov_data = iov;
	mul = phb->ioda.total_pe_num;

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &pdev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || res->parent)
			continue;
		if (!pnv_pci_is_m64_flags(res->flags)) {
			dev_warn(&pdev->dev, "Don't support SR-IOV with non M64 VF BAR%d: %pR. \n",
				 i, res);
			goto disable_iov;
		}

		vf_bar_sz = pci_iov_resource_size(pdev, i + PCI_IOV_RESOURCES);

		/*
		 * Generally, one segmented M64 BAR maps one IOV BAR. However,
		 * if a VF BAR is too large we end up wasting a lot of space.
		 * If each VF needs more than 1/4 of the default m64 segment
		 * then each VF BAR should be mapped in single-PE mode to reduce
		 * the amount of space required. This does however limit the
		 * number of VFs we can support.
		 *
		 * The 1/4 limit is arbitrary and can be tweaked.
		 */
		if (vf_bar_sz > (phb->ioda.m64_segsize >> 2)) {
			/*
			 * On PHB3, the minimum size alignment of M64 BAR in
			 * single mode is 32MB. If this VF BAR is smaller than
			 * 32MB, but still too large for a segmented window
			 * then we can't map it and need to disable SR-IOV for
			 * this device.
			 */
			if (vf_bar_sz < SZ_32M) {
				pci_err(pdev, "VF BAR%d: %pR can't be mapped in single PE mode\n",
					i, res);
				goto disable_iov;
			}

			iov->m64_single_mode[i] = true;
			continue;
		}

		/*
		 * This BAR can be mapped with one segmented window, so adjust
		 * te resource size to accommodate.
		 */
		pci_dbg(pdev, " Fixing VF BAR%d: %pR to\n", i, res);
		res->end = res->start + vf_bar_sz * mul - 1;
		pci_dbg(pdev, "                       %pR\n", res);

		pci_info(pdev, "VF BAR%d: %pR (expanded to %d VFs for PE alignment)",
			 i, res, mul);

		iov->need_shift = true;
	}

	return;

disable_iov:
	/* Save ourselves some MMIO space by disabling the unusable BARs */
	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &pdev->resource[i + PCI_IOV_RESOURCES];
		res->flags = 0;
		res->end = res->start - 1;
	}

	pdev->dev.archdata.iov_data = NULL;
	kfree(iov);
}

void pnv_pci_ioda_fixup_iov(struct pci_dev *pdev)
{
	if (pdev->is_virtfn) {
		struct pnv_ioda_pe *pe = pnv_ioda_get_pe(pdev);

		/*
		 * VF PEs are single-device PEs so their pdev pointer needs to
		 * be set. The pdev doesn't exist when the PE is allocated (in
		 * (pcibios_sriov_enable()) so we fix it up here.
		 */
		pe->pdev = pdev;
		WARN_ON(!(pe->flags & PNV_IODA_PE_VF));
	} else if (pdev->is_physfn) {
		/*
		 * For PFs adjust their allocated IOV resources to match what
		 * the PHB can support using its M64 BAR table.
		 */
		pnv_pci_ioda_fixup_iov_resources(pdev);
	}
}

resource_size_t pnv_pci_iov_resource_alignment(struct pci_dev *pdev,
						      int resno)
{
	resource_size_t align = pci_iov_resource_size(pdev, resno);
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct pnv_iov_data *iov = pnv_iov_get(pdev);

	/*
	 * iov can be null if we have an SR-IOV device with IOV BAR that can't
	 * be placed in the m64 space (i.e. The BAR is 32bit or non-prefetch).
	 * In that case we don't allow VFs to be enabled since one of their
	 * BARs would not be placed in the correct PE.
	 */
	if (!iov)
		return align;

	/*
	 * If we're using single mode then we can just use the native VF BAR
	 * alignment. We validated that it's possible to use a single PE
	 * window above when we did the fixup.
	 */
	if (iov->m64_single_mode[resno - PCI_IOV_RESOURCES])
		return align;

	/*
	 * On PowerNV platform, IOV BAR is mapped by M64 BAR to enable the
	 * SR-IOV. While from hardware perspective, the range mapped by M64
	 * BAR should be size aligned.
	 *
	 * This function returns the total IOV BAR size if M64 BAR is in
	 * Shared PE mode or just VF BAR size if not.
	 * If the M64 BAR is in Single PE mode, return the VF BAR size or
	 * M64 segment size if IOV BAR size is less.
	 */
	return phb->ioda.total_pe_num * align;
}

static int pnv_pci_vf_release_m64(struct pci_dev *pdev, u16 num_vfs)
{
	struct pnv_iov_data   *iov;
	struct pnv_phb        *phb;
	int window_id;

	phb = pci_bus_to_pnvhb(pdev->bus);
	iov = pnv_iov_get(pdev);

	for_each_set_bit(window_id, iov->used_m64_bar_mask, MAX_M64_BARS) {
		opal_pci_phb_mmio_enable(phb->opal_id,
					 OPAL_M64_WINDOW_TYPE,
					 window_id,
					 0);

		clear_bit(window_id, &phb->ioda.m64_bar_alloc);
	}

	return 0;
}


/*
 * PHB3 and beyond support segmented windows. The window's address range
 * is subdivided into phb->ioda.total_pe_num segments and there's a 1-1
 * mapping between PEs and segments.
 */
static int64_t pnv_ioda_map_m64_segmented(struct pnv_phb *phb,
					  int window_id,
					  resource_size_t start,
					  resource_size_t size)
{
	int64_t rc;

	rc = opal_pci_set_phb_mem_window(phb->opal_id,
					 OPAL_M64_WINDOW_TYPE,
					 window_id,
					 start,
					 0, /* unused */
					 size);
	if (rc)
		goto out;

	rc = opal_pci_phb_mmio_enable(phb->opal_id,
				      OPAL_M64_WINDOW_TYPE,
				      window_id,
				      OPAL_ENABLE_M64_SPLIT);
out:
	if (rc)
		pr_err("Failed to map M64 window #%d: %lld\n", window_id, rc);

	return rc;
}

static int64_t pnv_ioda_map_m64_single(struct pnv_phb *phb,
				       int pe_num,
				       int window_id,
				       resource_size_t start,
				       resource_size_t size)
{
	int64_t rc;

	/*
	 * The API for setting up m64 mmio windows seems to have been designed
	 * with P7-IOC in mind. For that chip each M64 BAR (window) had a fixed
	 * split of 8 equally sized segments each of which could individually
	 * assigned to a PE.
	 *
	 * The problem with this is that the API doesn't have any way to
	 * communicate the number of segments we want on a BAR. This wasn't
	 * a problem for p7-ioc since you didn't have a choice, but the
	 * single PE windows added in PHB3 don't map cleanly to this API.
	 *
	 * As a result we've got this slightly awkward process where we
	 * call opal_pci_map_pe_mmio_window() to put the single in single
	 * PE mode, and set the PE for the window before setting the address
	 * bounds. We need to do it this way because the single PE windows
	 * for PHB3 have different alignment requirements on PHB3.
	 */
	rc = opal_pci_map_pe_mmio_window(phb->opal_id,
					 pe_num,
					 OPAL_M64_WINDOW_TYPE,
					 window_id,
					 0);
	if (rc)
		goto out;

	/*
	 * NB: In single PE mode the window needs to be aligned to 32MB
	 */
	rc = opal_pci_set_phb_mem_window(phb->opal_id,
					 OPAL_M64_WINDOW_TYPE,
					 window_id,
					 start,
					 0, /* ignored by FW, m64 is 1-1 */
					 size);
	if (rc)
		goto out;

	/*
	 * Now actually enable it. We specified the BAR should be in "non-split"
	 * mode so FW will validate that the BAR is in single PE mode.
	 */
	rc = opal_pci_phb_mmio_enable(phb->opal_id,
				      OPAL_M64_WINDOW_TYPE,
				      window_id,
				      OPAL_ENABLE_M64_NON_SPLIT);
out:
	if (rc)
		pr_err("Error mapping single PE BAR\n");

	return rc;
}

static int pnv_pci_alloc_m64_bar(struct pnv_phb *phb, struct pnv_iov_data *iov)
{
	int win;

	do {
		win = find_next_zero_bit(&phb->ioda.m64_bar_alloc,
				phb->ioda.m64_bar_idx + 1, 0);

		if (win >= phb->ioda.m64_bar_idx + 1)
			return -1;
	} while (test_and_set_bit(win, &phb->ioda.m64_bar_alloc));

	set_bit(win, iov->used_m64_bar_mask);

	return win;
}

static int pnv_pci_vf_assign_m64(struct pci_dev *pdev, u16 num_vfs)
{
	struct pnv_iov_data   *iov;
	struct pnv_phb        *phb;
	int                    win;
	struct resource       *res;
	int                    i, j;
	int64_t                rc;
	resource_size_t        size, start;
	int                    base_pe_num;

	phb = pci_bus_to_pnvhb(pdev->bus);
	iov = pnv_iov_get(pdev);

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &pdev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || !res->parent)
			continue;

		/* don't need single mode? map everything in one go! */
		if (!iov->m64_single_mode[i]) {
			win = pnv_pci_alloc_m64_bar(phb, iov);
			if (win < 0)
				goto m64_failed;

			size = resource_size(res);
			start = res->start;

			rc = pnv_ioda_map_m64_segmented(phb, win, start, size);
			if (rc)
				goto m64_failed;

			continue;
		}

		/* otherwise map each VF with single PE BARs */
		size = pci_iov_resource_size(pdev, PCI_IOV_RESOURCES + i);
		base_pe_num = iov->vf_pe_arr[0].pe_number;

		for (j = 0; j < num_vfs; j++) {
			win = pnv_pci_alloc_m64_bar(phb, iov);
			if (win < 0)
				goto m64_failed;

			start = res->start + size * j;
			rc = pnv_ioda_map_m64_single(phb, win,
						     base_pe_num + j,
						     start,
						     size);
			if (rc)
				goto m64_failed;
		}
	}
	return 0;

m64_failed:
	pnv_pci_vf_release_m64(pdev, num_vfs);
	return -EBUSY;
}

static void pnv_ioda_release_vf_PE(struct pci_dev *pdev)
{
	struct pnv_phb        *phb;
	struct pnv_ioda_pe    *pe, *pe_n;

	phb = pci_bus_to_pnvhb(pdev->bus);

	if (!pdev->is_physfn)
		return;

	/* FIXME: Use pnv_ioda_release_pe()? */
	list_for_each_entry_safe(pe, pe_n, &phb->ioda.pe_list, list) {
		if (pe->parent_dev != pdev)
			continue;

		pnv_pci_ioda2_release_pe_dma(pe);

		/* Remove from list */
		mutex_lock(&phb->ioda.pe_list_mutex);
		list_del(&pe->list);
		mutex_unlock(&phb->ioda.pe_list_mutex);

		pnv_ioda_deconfigure_pe(phb, pe);

		pnv_ioda_free_pe(pe);
	}
}

static int pnv_pci_vf_resource_shift(struct pci_dev *dev, int offset)
{
	struct resource *res, res2;
	struct pnv_iov_data *iov;
	resource_size_t size;
	u16 num_vfs;
	int i;

	if (!dev->is_physfn)
		return -EINVAL;
	iov = pnv_iov_get(dev);

	/*
	 * "offset" is in VFs.  The M64 windows are sized so that when they
	 * are segmented, each segment is the same size as the IOV BAR.
	 * Each segment is in a separate PE, and the high order bits of the
	 * address are the PE number.  Therefore, each VF's BAR is in a
	 * separate PE, and changing the IOV BAR start address changes the
	 * range of PEs the VFs are in.
	 */
	num_vfs = iov->num_vfs;
	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &dev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || !res->parent)
			continue;
		if (iov->m64_single_mode[i])
			continue;

		/*
		 * The actual IOV BAR range is determined by the start address
		 * and the actual size for num_vfs VFs BAR.  This check is to
		 * make sure that after shifting, the range will not overlap
		 * with another device.
		 */
		size = pci_iov_resource_size(dev, i + PCI_IOV_RESOURCES);
		res2.flags = res->flags;
		res2.start = res->start + (size * offset);
		res2.end = res2.start + (size * num_vfs) - 1;

		if (res2.end > res->end) {
			dev_err(&dev->dev, "VF BAR%d: %pR would extend past %pR (trying to enable %d VFs shifted by %d)\n",
				i, &res2, res, num_vfs, offset);
			return -EBUSY;
		}
	}

	/*
	 * Since M64 BAR shares segments among all possible 256 PEs,
	 * we have to shift the beginning of PF IOV BAR to make it start from
	 * the segment which belongs to the PE number assigned to the first VF.
	 * This creates a "hole" in the /proc/iomem which could be used for
	 * allocating other resources so we reserve this area below and
	 * release when IOV is released.
	 */
	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &dev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || !res->parent)
			continue;
		if (iov->m64_single_mode[i])
			continue;

		size = pci_iov_resource_size(dev, i + PCI_IOV_RESOURCES);
		res2 = *res;
		res->start += size * offset;

		dev_info(&dev->dev, "VF BAR%d: %pR shifted to %pR (%sabling %d VFs shifted by %d)\n",
			 i, &res2, res, (offset > 0) ? "En" : "Dis",
			 num_vfs, offset);

		if (offset < 0) {
			devm_release_resource(&dev->dev, &iov->holes[i]);
			memset(&iov->holes[i], 0, sizeof(iov->holes[i]));
		}

		pci_update_resource(dev, i + PCI_IOV_RESOURCES);

		if (offset > 0) {
			iov->holes[i].start = res2.start;
			iov->holes[i].end = res2.start + size * offset - 1;
			iov->holes[i].flags = IORESOURCE_BUS;
			iov->holes[i].name = "pnv_iov_reserved";
			devm_request_resource(&dev->dev, res->parent,
					&iov->holes[i]);
		}
	}
	return 0;
}

static void pnv_pci_sriov_disable(struct pci_dev *pdev)
{
	u16                    num_vfs, base_pe;
	struct pnv_iov_data   *iov;

	iov = pnv_iov_get(pdev);
	if (WARN_ON(!iov))
		return;

	num_vfs = iov->num_vfs;
	base_pe = iov->vf_pe_arr[0].pe_number;

	/* Release VF PEs */
	pnv_ioda_release_vf_PE(pdev);

	/* Un-shift the IOV BARs if we need to */
	if (iov->need_shift)
		pnv_pci_vf_resource_shift(pdev, -base_pe);

	/* Release M64 windows */
	pnv_pci_vf_release_m64(pdev, num_vfs);
}

static void pnv_ioda_setup_vf_PE(struct pci_dev *pdev, u16 num_vfs)
{
	struct pnv_phb        *phb;
	struct pnv_ioda_pe    *pe;
	int                    pe_num;
	u16                    vf_index;
	struct pnv_iov_data   *iov;
	struct pci_dn         *pdn;

	if (!pdev->is_physfn)
		return;

	phb = pci_bus_to_pnvhb(pdev->bus);
	pdn = pci_get_pdn(pdev);
	iov = pnv_iov_get(pdev);

	/* Reserve PE for each VF */
	for (vf_index = 0; vf_index < num_vfs; vf_index++) {
		int vf_devfn = pci_iov_virtfn_devfn(pdev, vf_index);
		int vf_bus = pci_iov_virtfn_bus(pdev, vf_index);
		struct pci_dn *vf_pdn;

		pe = &iov->vf_pe_arr[vf_index];
		pe->phb = phb;
		pe->flags = PNV_IODA_PE_VF;
		pe->pbus = NULL;
		pe->parent_dev = pdev;
		pe->mve_number = -1;
		pe->rid = (vf_bus << 8) | vf_devfn;

		pe_num = pe->pe_number;
		pe_info(pe, "VF %04d:%02d:%02d.%d associated with PE#%x\n",
			pci_domain_nr(pdev->bus), pdev->bus->number,
			PCI_SLOT(vf_devfn), PCI_FUNC(vf_devfn), pe_num);

		if (pnv_ioda_configure_pe(phb, pe)) {
			/* XXX What do we do here ? */
			pnv_ioda_free_pe(pe);
			pe->pdev = NULL;
			continue;
		}

		/* Put PE to the list */
		mutex_lock(&phb->ioda.pe_list_mutex);
		list_add_tail(&pe->list, &phb->ioda.pe_list);
		mutex_unlock(&phb->ioda.pe_list_mutex);

		/* associate this pe to its pdn */
		list_for_each_entry(vf_pdn, &pdn->parent->child_list, list) {
			if (vf_pdn->busno == vf_bus &&
			    vf_pdn->devfn == vf_devfn) {
				vf_pdn->pe_number = pe_num;
				break;
			}
		}

		pnv_pci_ioda2_setup_dma_pe(phb, pe);
	}
}

static int pnv_pci_sriov_enable(struct pci_dev *pdev, u16 num_vfs)
{
	struct pnv_ioda_pe    *base_pe;
	struct pnv_iov_data   *iov;
	struct pnv_phb        *phb;
	int                    ret;
	u16                    i;

	phb = pci_bus_to_pnvhb(pdev->bus);
	iov = pnv_iov_get(pdev);

	/*
	 * There's a calls to IODA2 PE setup code littered throughout. We could
	 * probably fix that, but we'd still have problems due to the
	 * restriction inherent on IODA1 PHBs.
	 *
	 * NB: We class IODA3 as IODA2 since they're very similar.
	 */
	if (phb->type != PNV_PHB_IODA2) {
		pci_err(pdev, "SR-IOV is not supported on this PHB\n");
		return -ENXIO;
	}

	if (!iov) {
		dev_info(&pdev->dev, "don't support this SRIOV device with non 64bit-prefetchable IOV BAR\n");
		return -ENOSPC;
	}

	/* allocate a contiguous block of PEs for our VFs */
	base_pe = pnv_ioda_alloc_pe(phb, num_vfs);
	if (!base_pe) {
		pci_err(pdev, "Unable to allocate PEs for %d VFs\n", num_vfs);
		return -EBUSY;
	}

	iov->vf_pe_arr = base_pe;
	iov->num_vfs = num_vfs;

	/* Assign M64 window accordingly */
	ret = pnv_pci_vf_assign_m64(pdev, num_vfs);
	if (ret) {
		dev_info(&pdev->dev, "Not enough M64 window resources\n");
		goto m64_failed;
	}

	/*
	 * When using one M64 BAR to map one IOV BAR, we need to shift
	 * the IOV BAR according to the PE# allocated to the VFs.
	 * Otherwise, the PE# for the VF will conflict with others.
	 */
	if (iov->need_shift) {
		ret = pnv_pci_vf_resource_shift(pdev, base_pe->pe_number);
		if (ret)
			goto shift_failed;
	}

	/* Setup VF PEs */
	pnv_ioda_setup_vf_PE(pdev, num_vfs);

	return 0;

shift_failed:
	pnv_pci_vf_release_m64(pdev, num_vfs);

m64_failed:
	for (i = 0; i < num_vfs; i++)
		pnv_ioda_free_pe(&iov->vf_pe_arr[i]);

	return ret;
}

int pnv_pcibios_sriov_disable(struct pci_dev *pdev)
{
	pnv_pci_sriov_disable(pdev);

	/* Release PCI data */
	remove_sriov_vf_pdns(pdev);
	return 0;
}

int pnv_pcibios_sriov_enable(struct pci_dev *pdev, u16 num_vfs)
{
	/* Allocate PCI data */
	add_sriov_vf_pdns(pdev);

	return pnv_pci_sriov_enable(pdev, num_vfs);
}
