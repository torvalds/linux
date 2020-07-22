// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/bitmap.h>
#include <linux/pci.h>

#include <asm/opal.h>

#include "pci.h"

/* for pci_dev_is_added() */
#include "../../../../drivers/pci/pci.h"

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
 * are not coupled. The alignment is set based on the the per-VF BAR size, but
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
 * 1. In pcibios_add_device() we call pnv_pci_ioda_fixup_iov().
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
	const resource_size_t gate = phb->ioda.m64_segsize >> 2;
	struct resource *res;
	int i;
	resource_size_t size, total_vf_bar_sz;
	struct pnv_iov_data *iov;
	int mul, total_vfs;

	iov = kzalloc(sizeof(*iov), GFP_KERNEL);
	if (!iov)
		goto disable_iov;
	pdev->dev.archdata.iov_data = iov;

	total_vfs = pci_sriov_get_totalvfs(pdev);
	mul = phb->ioda.total_pe_num;
	total_vf_bar_sz = 0;

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &pdev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || res->parent)
			continue;
		if (!pnv_pci_is_m64_flags(res->flags)) {
			dev_warn(&pdev->dev, "Don't support SR-IOV with non M64 VF BAR%d: %pR. \n",
				 i, res);
			goto disable_iov;
		}

		total_vf_bar_sz += pci_iov_resource_size(pdev,
				i + PCI_IOV_RESOURCES);

		/*
		 * If bigger than quarter of M64 segment size, just round up
		 * power of two.
		 *
		 * Generally, one M64 BAR maps one IOV BAR. To avoid conflict
		 * with other devices, IOV BAR size is expanded to be
		 * (total_pe * VF_BAR_size).  When VF_BAR_size is half of M64
		 * segment size , the expanded size would equal to half of the
		 * whole M64 space size, which will exhaust the M64 Space and
		 * limit the system flexibility.  This is a design decision to
		 * set the boundary to quarter of the M64 segment size.
		 */
		if (total_vf_bar_sz > gate) {
			mul = roundup_pow_of_two(total_vfs);
			dev_info(&pdev->dev,
				"VF BAR Total IOV size %llx > %llx, roundup to %d VFs\n",
				total_vf_bar_sz, gate, mul);
			iov->m64_single_mode = true;
			break;
		}
	}

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &pdev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || res->parent)
			continue;

		size = pci_iov_resource_size(pdev, i + PCI_IOV_RESOURCES);
		/*
		 * On PHB3, the minimum size alignment of M64 BAR in single
		 * mode is 32MB.
		 */
		if (iov->m64_single_mode && (size < SZ_32M))
			goto disable_iov;

		dev_dbg(&pdev->dev, " Fixing VF BAR%d: %pR to\n", i, res);
		res->end = res->start + size * mul - 1;
		dev_dbg(&pdev->dev, "                       %pR\n", res);
		dev_info(&pdev->dev, "VF BAR%d: %pR (expanded to %d VFs for PE alignment)",
			 i, res, mul);
	}
	iov->vfs_expanded = mul;

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
	if (WARN_ON(pci_dev_is_added(pdev)))
		return;

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
		 * the PHB can support using it's M64 BAR table.
		 */
		pnv_pci_ioda_fixup_iov_resources(pdev);
	}
}

resource_size_t pnv_pci_iov_resource_alignment(struct pci_dev *pdev,
						      int resno)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct pnv_iov_data *iov = pnv_iov_get(pdev);
	resource_size_t align;

	/*
	 * On PowerNV platform, IOV BAR is mapped by M64 BAR to enable the
	 * SR-IOV. While from hardware perspective, the range mapped by M64
	 * BAR should be size aligned.
	 *
	 * When IOV BAR is mapped with M64 BAR in Single PE mode, the extra
	 * powernv-specific hardware restriction is gone. But if just use the
	 * VF BAR size as the alignment, PF BAR / VF BAR may be allocated with
	 * in one segment of M64 #15, which introduces the PE conflict between
	 * PF and VF. Based on this, the minimum alignment of an IOV BAR is
	 * m64_segsize.
	 *
	 * This function returns the total IOV BAR size if M64 BAR is in
	 * Shared PE mode or just VF BAR size if not.
	 * If the M64 BAR is in Single PE mode, return the VF BAR size or
	 * M64 segment size if IOV BAR size is less.
	 */
	align = pci_iov_resource_size(pdev, resno);

	/*
	 * iov can be null if we have an SR-IOV device with IOV BAR that can't
	 * be placed in the m64 space (i.e. The BAR is 32bit or non-prefetch).
	 * In that case we don't allow VFs to be enabled so just return the
	 * default alignment.
	 */
	if (!iov)
		return align;
	if (!iov->vfs_expanded)
		return align;
	if (iov->m64_single_mode)
		return max(align, (resource_size_t)phb->ioda.m64_segsize);

	return iov->vfs_expanded * align;
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

static int pnv_pci_vf_assign_m64(struct pci_dev *pdev, u16 num_vfs)
{
	struct pnv_iov_data   *iov;
	struct pnv_phb        *phb;
	unsigned int           win;
	struct resource       *res;
	int                    i, j;
	int64_t                rc;
	int                    total_vfs;
	resource_size_t        size, start;
	int                    pe_num;
	int                    m64_bars;

	phb = pci_bus_to_pnvhb(pdev->bus);
	iov = pnv_iov_get(pdev);
	total_vfs = pci_sriov_get_totalvfs(pdev);

	if (iov->m64_single_mode)
		m64_bars = num_vfs;
	else
		m64_bars = 1;

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		res = &pdev->resource[i + PCI_IOV_RESOURCES];
		if (!res->flags || !res->parent)
			continue;

		for (j = 0; j < m64_bars; j++) {

			/* allocate a window ID for this BAR */
			do {
				win = find_next_zero_bit(&phb->ioda.m64_bar_alloc,
						phb->ioda.m64_bar_idx + 1, 0);

				if (win >= phb->ioda.m64_bar_idx + 1)
					goto m64_failed;
			} while (test_and_set_bit(win, &phb->ioda.m64_bar_alloc));
			set_bit(win, iov->used_m64_bar_mask);

			if (iov->m64_single_mode) {
				size = pci_iov_resource_size(pdev,
							PCI_IOV_RESOURCES + i);
				start = res->start + size * j;
			} else {
				size = resource_size(res);
				start = res->start;
			}

			/* Map the M64 here */
			if (iov->m64_single_mode) {
				pe_num = iov->pe_num_map[j];
				rc = opal_pci_map_pe_mmio_window(phb->opal_id,
						pe_num, OPAL_M64_WINDOW_TYPE,
						win, 0);
			}

			rc = opal_pci_set_phb_mem_window(phb->opal_id,
						 OPAL_M64_WINDOW_TYPE,
						 win,
						 start,
						 0, /* unused */
						 size);


			if (rc != OPAL_SUCCESS) {
				dev_err(&pdev->dev, "Failed to map M64 window #%d: %lld\n",
					win, rc);
				goto m64_failed;
			}

			if (iov->m64_single_mode)
				rc = opal_pci_phb_mmio_enable(phb->opal_id,
				     OPAL_M64_WINDOW_TYPE, win, 2);
			else
				rc = opal_pci_phb_mmio_enable(phb->opal_id,
				     OPAL_M64_WINDOW_TYPE, win, 1);

			if (rc != OPAL_SUCCESS) {
				dev_err(&pdev->dev, "Failed to enable M64 window #%d: %llx\n",
					win, rc);
				goto m64_failed;
			}
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
	struct pnv_phb        *phb;
	struct pnv_ioda_pe    *pe;
	struct pnv_iov_data   *iov;
	u16                    num_vfs, i;

	phb = pci_bus_to_pnvhb(pdev->bus);
	iov = pnv_iov_get(pdev);
	num_vfs = iov->num_vfs;

	/* Release VF PEs */
	pnv_ioda_release_vf_PE(pdev);

	if (phb->type == PNV_PHB_IODA2) {
		if (!iov->m64_single_mode)
			pnv_pci_vf_resource_shift(pdev, -*iov->pe_num_map);

		/* Release M64 windows */
		pnv_pci_vf_release_m64(pdev, num_vfs);

		/* Release PE numbers */
		if (iov->m64_single_mode) {
			for (i = 0; i < num_vfs; i++) {
				if (iov->pe_num_map[i] == IODA_INVALID_PE)
					continue;

				pe = &phb->ioda.pe_array[iov->pe_num_map[i]];
				pnv_ioda_free_pe(pe);
			}
		} else
			bitmap_clear(phb->ioda.pe_alloc, *iov->pe_num_map, num_vfs);
		/* Releasing pe_num_map */
		kfree(iov->pe_num_map);
	}
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

		if (iov->m64_single_mode)
			pe_num = iov->pe_num_map[vf_index];
		else
			pe_num = *iov->pe_num_map + vf_index;

		pe = &phb->ioda.pe_array[pe_num];
		pe->pe_number = pe_num;
		pe->phb = phb;
		pe->flags = PNV_IODA_PE_VF;
		pe->pbus = NULL;
		pe->parent_dev = pdev;
		pe->mve_number = -1;
		pe->rid = (vf_bus << 8) | vf_devfn;

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

		/* associate this pe to it's pdn */
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
	struct pnv_iov_data   *iov;
	struct pnv_phb        *phb;
	struct pnv_ioda_pe    *pe;
	int                    ret;
	u16                    i;

	phb = pci_bus_to_pnvhb(pdev->bus);
	iov = pnv_iov_get(pdev);

	if (phb->type == PNV_PHB_IODA2) {
		if (!iov->vfs_expanded) {
			dev_info(&pdev->dev,
				 "don't support this SRIOV device with non 64bit-prefetchable IOV BAR\n");
			return -ENOSPC;
		}

		/*
		 * When M64 BARs functions in Single PE mode, the number of VFs
		 * could be enabled must be less than the number of M64 BARs.
		 */
		if (iov->m64_single_mode && num_vfs > phb->ioda.m64_bar_idx) {
			dev_info(&pdev->dev, "Not enough M64 BAR for VFs\n");
			return -EBUSY;
		}

		/* Allocating pe_num_map */
		if (iov->m64_single_mode)
			iov->pe_num_map = kmalloc_array(num_vfs,
							sizeof(*iov->pe_num_map),
							GFP_KERNEL);
		else
			iov->pe_num_map = kmalloc(sizeof(*iov->pe_num_map), GFP_KERNEL);

		if (!iov->pe_num_map)
			return -ENOMEM;

		if (iov->m64_single_mode)
			for (i = 0; i < num_vfs; i++)
				iov->pe_num_map[i] = IODA_INVALID_PE;

		/* Calculate available PE for required VFs */
		if (iov->m64_single_mode) {
			for (i = 0; i < num_vfs; i++) {
				pe = pnv_ioda_alloc_pe(phb);
				if (!pe) {
					ret = -EBUSY;
					goto m64_failed;
				}

				iov->pe_num_map[i] = pe->pe_number;
			}
		} else {
			mutex_lock(&phb->ioda.pe_alloc_mutex);
			*iov->pe_num_map = bitmap_find_next_zero_area(
				phb->ioda.pe_alloc, phb->ioda.total_pe_num,
				0, num_vfs, 0);
			if (*iov->pe_num_map >= phb->ioda.total_pe_num) {
				mutex_unlock(&phb->ioda.pe_alloc_mutex);
				dev_info(&pdev->dev, "Failed to enable VF%d\n", num_vfs);
				kfree(iov->pe_num_map);
				return -EBUSY;
			}
			bitmap_set(phb->ioda.pe_alloc, *iov->pe_num_map, num_vfs);
			mutex_unlock(&phb->ioda.pe_alloc_mutex);
		}
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
		if (!iov->m64_single_mode) {
			ret = pnv_pci_vf_resource_shift(pdev, *iov->pe_num_map);
			if (ret)
				goto m64_failed;
		}
	}

	/* Setup VF PEs */
	pnv_ioda_setup_vf_PE(pdev, num_vfs);

	return 0;

m64_failed:
	if (iov->m64_single_mode) {
		for (i = 0; i < num_vfs; i++) {
			if (iov->pe_num_map[i] == IODA_INVALID_PE)
				continue;

			pe = &phb->ioda.pe_array[iov->pe_num_map[i]];
			pnv_ioda_free_pe(pe);
		}
	} else
		bitmap_clear(phb->ioda.pe_alloc, *iov->pe_num_map, num_vfs);

	/* Releasing pe_num_map */
	kfree(iov->pe_num_map);

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
