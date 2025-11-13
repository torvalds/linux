// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Resizable BAR Extended Capability handling.
 */

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "pci.h"

void pci_rebar_init(struct pci_dev *pdev)
{
	pdev->rebar_cap = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_REBAR);
}

/**
 * pci_rebar_find_pos - find position of resize ctrl reg for BAR
 * @pdev: PCI device
 * @bar: BAR to find
 *
 * Helper to find the position of the ctrl register for a BAR.
 * Returns -ENOTSUPP if resizable BARs are not supported at all.
 * Returns -ENOENT if no ctrl register for the BAR could be found.
 */
static int pci_rebar_find_pos(struct pci_dev *pdev, int bar)
{
	unsigned int pos, nbars, i;
	u32 ctrl;

	if (pci_resource_is_iov(bar)) {
		pos = pci_iov_vf_rebar_cap(pdev);
		bar = pci_resource_num_to_vf_bar(bar);
	} else {
		pos = pdev->rebar_cap;
	}

	if (!pos)
		return -ENOTSUPP;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	nbars = FIELD_GET(PCI_REBAR_CTRL_NBAR_MASK, ctrl);

	for (i = 0; i < nbars; i++, pos += 8) {
		int bar_idx;

		pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
		bar_idx = FIELD_GET(PCI_REBAR_CTRL_BAR_IDX, ctrl);
		if (bar_idx == bar)
			return pos;
	}

	return -ENOENT;
}

/**
 * pci_rebar_get_possible_sizes - get possible sizes for BAR
 * @pdev: PCI device
 * @bar: BAR to query
 *
 * Get the possible sizes of a resizable BAR as bitmask defined in the spec
 * (bit 0=1MB, bit 31=128TB). Returns 0 if BAR isn't resizable.
 */
u32 pci_rebar_get_possible_sizes(struct pci_dev *pdev, int bar)
{
	int pos;
	u32 cap;

	pos = pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return 0;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CAP, &cap);
	cap = FIELD_GET(PCI_REBAR_CAP_SIZES, cap);

	/* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
	if (pdev->vendor == PCI_VENDOR_ID_ATI && pdev->device == 0x731f &&
	    bar == 0 && cap == 0x700)
		return 0x3f00;

	return cap;
}
EXPORT_SYMBOL(pci_rebar_get_possible_sizes);

/**
 * pci_rebar_get_current_size - get the current size of a BAR
 * @pdev: PCI device
 * @bar: BAR to set size to
 *
 * Read the size of a BAR from the resizable BAR config.
 * Returns size if found or negative error code.
 */
int pci_rebar_get_current_size(struct pci_dev *pdev, int bar)
{
	int pos;
	u32 ctrl;

	pos = pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return pos;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	return FIELD_GET(PCI_REBAR_CTRL_BAR_SIZE, ctrl);
}

/**
 * pci_rebar_set_size - set a new size for a BAR
 * @pdev: PCI device
 * @bar: BAR to set size to
 * @size: new size as defined in the spec (0=1MB, 31=128TB)
 *
 * Set the new size of a BAR as defined in the spec.
 * Returns zero if resizing was successful, error code otherwise.
 */
int pci_rebar_set_size(struct pci_dev *pdev, int bar, int size)
{
	int pos;
	u32 ctrl;

	pos = pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return pos;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	ctrl &= ~PCI_REBAR_CTRL_BAR_SIZE;
	ctrl |= FIELD_PREP(PCI_REBAR_CTRL_BAR_SIZE, size);
	pci_write_config_dword(pdev, pos + PCI_REBAR_CTRL, ctrl);

	if (pci_resource_is_iov(bar))
		pci_iov_resource_set_size(pdev, bar, size);

	return 0;
}

void pci_restore_rebar_state(struct pci_dev *pdev)
{
	unsigned int pos, nbars, i;
	u32 ctrl;

	pos = pdev->rebar_cap;
	if (!pos)
		return;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	nbars = FIELD_GET(PCI_REBAR_CTRL_NBAR_MASK, ctrl);

	for (i = 0; i < nbars; i++, pos += 8) {
		struct resource *res;
		int bar_idx, size;

		pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
		bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
		res = pci_resource_n(pdev, bar_idx);
		size = pci_rebar_bytes_to_size(resource_size(res));
		ctrl &= ~PCI_REBAR_CTRL_BAR_SIZE;
		ctrl |= FIELD_PREP(PCI_REBAR_CTRL_BAR_SIZE, size);
		pci_write_config_dword(pdev, pos + PCI_REBAR_CTRL, ctrl);
	}
}

static bool pci_resize_is_memory_decoding_enabled(struct pci_dev *dev,
						  int resno)
{
	u16 cmd;

	if (pci_resource_is_iov(resno))
		return pci_iov_is_memory_decoding_enabled(dev);

	pci_read_config_word(dev, PCI_COMMAND, &cmd);

	return cmd & PCI_COMMAND_MEMORY;
}

void pci_resize_resource_set_size(struct pci_dev *dev, int resno, int size)
{
	resource_size_t res_size = pci_rebar_size_to_bytes(size);
	struct resource *res = pci_resource_n(dev, resno);

	if (pci_resource_is_iov(resno))
		res_size *= pci_sriov_get_totalvfs(dev);

	resource_set_size(res, res_size);
}

/**
 * pci_resize_resource - reconfigure a Resizable BAR and resources
 * @dev: the PCI device
 * @resno: index of the BAR to be resized
 * @size: new size as defined in the spec (0=1MB, 31=128TB)
 * @exclude_bars: a mask of BARs that should not be released
 *
 * Reconfigure @resno to @size and re-run resource assignment algorithm
 * with the new size.
 *
 * Prior to resize, release @dev resources that share a bridge window with
 * @resno.  This unpins the bridge window resource to allow changing it.
 *
 * The caller may prevent releasing a particular BAR by providing
 * @exclude_bars mask, but this may result in the resize operation failing
 * due to insufficient space.
 *
 * Return: 0 on success, or negative on error. In case of an error, the
 *         resources are restored to their original places.
 */
int pci_resize_resource(struct pci_dev *dev, int resno, int size,
			int exclude_bars)
{
	struct pci_host_bridge *host;
	int old, ret;
	u32 sizes;

	/* Check if we must preserve the firmware's resource assignment */
	host = pci_find_host_bridge(dev->bus);
	if (host->preserve_config)
		return -ENOTSUPP;

	if (pci_resize_is_memory_decoding_enabled(dev, resno))
		return -EBUSY;

	sizes = pci_rebar_get_possible_sizes(dev, resno);
	if (!sizes)
		return -ENOTSUPP;

	if (!(sizes & BIT(size)))
		return -EINVAL;

	old = pci_rebar_get_current_size(dev, resno);
	if (old < 0)
		return old;

	ret = pci_rebar_set_size(dev, resno, size);
	if (ret)
		return ret;

	ret = pci_do_resource_release_and_resize(dev, resno, size, exclude_bars);
	if (ret)
		goto error_resize;
	return 0;

error_resize:
	pci_rebar_set_size(dev, resno, old);
	return ret;
}
EXPORT_SYMBOL(pci_resize_resource);
