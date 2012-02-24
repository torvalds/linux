/*
 * Copyright (c) 2008, 2009 QLogic Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/aer.h>
#include <linux/module.h>

#include "qib.h"

/*
 * This file contains PCIe utility routines that are common to the
 * various QLogic InfiniPath adapters
 */

/*
 * Code to adjust PCIe capabilities.
 * To minimize the change footprint, we call it
 * from qib_pcie_params, which every chip-specific
 * file calls, even though this violates some
 * expectations of harmlessness.
 */
static int qib_tune_pcie_caps(struct qib_devdata *);
static int qib_tune_pcie_coalesce(struct qib_devdata *);

/*
 * Do all the common PCIe setup and initialization.
 * devdata is not yet allocated, and is not allocated until after this
 * routine returns success.  Therefore qib_dev_err() can't be used for error
 * printing.
 */
int qib_pcie_init(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		/*
		 * This can happen (in theory) iff:
		 * We did a chip reset, and then failed to reprogram the
		 * BAR, or the chip reset due to an internal error.  We then
		 * unloaded the driver and reloaded it.
		 *
		 * Both reset cases set the BAR back to initial state.  For
		 * the latter case, the AER sticky error bit at offset 0x718
		 * should be set, but the Linux kernel doesn't yet know
		 * about that, it appears.  If the original BAR was retained
		 * in the kernel data structures, this may be OK.
		 */
		qib_early_err(&pdev->dev, "pci enable failed: error %d\n",
			      -ret);
		goto done;
	}

	ret = pci_request_regions(pdev, QIB_DRV_NAME);
	if (ret) {
		qib_devinfo(pdev, "pci_request_regions fails: err %d\n", -ret);
		goto bail;
	}

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret) {
		/*
		 * If the 64 bit setup fails, try 32 bit.  Some systems
		 * do not setup 64 bit maps on systems with 2GB or less
		 * memory installed.
		 */
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			qib_devinfo(pdev, "Unable to set DMA mask: %d\n", ret);
			goto bail;
		}
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	} else
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret) {
		qib_early_err(&pdev->dev,
			      "Unable to set DMA consistent mask: %d\n", ret);
		goto bail;
	}

	pci_set_master(pdev);
	ret = pci_enable_pcie_error_reporting(pdev);
	if (ret) {
		qib_early_err(&pdev->dev,
			      "Unable to enable pcie error reporting: %d\n",
			      ret);
		ret = 0;
	}
	goto done;

bail:
	pci_disable_device(pdev);
	pci_release_regions(pdev);
done:
	return ret;
}

/*
 * Do remaining PCIe setup, once dd is allocated, and save away
 * fields required to re-initialize after a chip reset, or for
 * various other purposes
 */
int qib_pcie_ddinit(struct qib_devdata *dd, struct pci_dev *pdev,
		    const struct pci_device_id *ent)
{
	unsigned long len;
	resource_size_t addr;

	dd->pcidev = pdev;
	pci_set_drvdata(pdev, dd);

	addr = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);

#if defined(__powerpc__)
	/* There isn't a generic way to specify writethrough mappings */
	dd->kregbase = __ioremap(addr, len, _PAGE_NO_CACHE | _PAGE_WRITETHRU);
#else
	dd->kregbase = ioremap_nocache(addr, len);
#endif

	if (!dd->kregbase)
		return -ENOMEM;

	dd->kregend = (u64 __iomem *)((void __iomem *) dd->kregbase + len);
	dd->physaddr = addr;        /* used for io_remap, etc. */

	/*
	 * Save BARs to rewrite after device reset.  Save all 64 bits of
	 * BAR, just in case.
	 */
	dd->pcibar0 = addr;
	dd->pcibar1 = addr >> 32;
	dd->deviceid = ent->device; /* save for later use */
	dd->vendorid = ent->vendor;

	return 0;
}

/*
 * Do PCIe cleanup, after chip-specific cleanup, etc.  Just prior
 * to releasing the dd memory.
 * void because none of the core pcie cleanup returns are void
 */
void qib_pcie_ddcleanup(struct qib_devdata *dd)
{
	u64 __iomem *base = (void __iomem *) dd->kregbase;

	dd->kregbase = NULL;
	iounmap(base);
	if (dd->piobase)
		iounmap(dd->piobase);
	if (dd->userbase)
		iounmap(dd->userbase);
	if (dd->piovl15base)
		iounmap(dd->piovl15base);

	pci_disable_device(dd->pcidev);
	pci_release_regions(dd->pcidev);

	pci_set_drvdata(dd->pcidev, NULL);
}

static void qib_msix_setup(struct qib_devdata *dd, int pos, u32 *msixcnt,
			   struct msix_entry *msix_entry)
{
	int ret;
	u32 tabsize = 0;
	u16 msix_flags;

	pci_read_config_word(dd->pcidev, pos + PCI_MSIX_FLAGS, &msix_flags);
	tabsize = 1 + (msix_flags & PCI_MSIX_FLAGS_QSIZE);
	if (tabsize > *msixcnt)
		tabsize = *msixcnt;
	ret = pci_enable_msix(dd->pcidev, msix_entry, tabsize);
	if (ret > 0) {
		tabsize = ret;
		ret = pci_enable_msix(dd->pcidev, msix_entry, tabsize);
	}
	if (ret) {
		qib_dev_err(dd, "pci_enable_msix %d vectors failed: %d, "
			    "falling back to INTx\n", tabsize, ret);
		tabsize = 0;
	}
	*msixcnt = tabsize;

	if (ret)
		qib_enable_intx(dd->pcidev);

}

/**
 * We save the msi lo and hi values, so we can restore them after
 * chip reset (the kernel PCI infrastructure doesn't yet handle that
 * correctly.
 */
static int qib_msi_setup(struct qib_devdata *dd, int pos)
{
	struct pci_dev *pdev = dd->pcidev;
	u16 control;
	int ret;

	ret = pci_enable_msi(pdev);
	if (ret)
		qib_dev_err(dd, "pci_enable_msi failed: %d, "
			    "interrupts may not work\n", ret);
	/* continue even if it fails, we may still be OK... */

	pci_read_config_dword(pdev, pos + PCI_MSI_ADDRESS_LO,
			      &dd->msi_lo);
	pci_read_config_dword(pdev, pos + PCI_MSI_ADDRESS_HI,
			      &dd->msi_hi);
	pci_read_config_word(pdev, pos + PCI_MSI_FLAGS, &control);
	/* now save the data (vector) info */
	pci_read_config_word(pdev, pos + ((control & PCI_MSI_FLAGS_64BIT)
				    ? 12 : 8),
			     &dd->msi_data);
	return ret;
}

int qib_pcie_params(struct qib_devdata *dd, u32 minw, u32 *nent,
		    struct msix_entry *entry)
{
	u16 linkstat, speed;
	int pos = 0, pose, ret = 1;

	pose = pci_pcie_cap(dd->pcidev);
	if (!pose) {
		qib_dev_err(dd, "Can't find PCI Express capability!\n");
		/* set up something... */
		dd->lbus_width = 1;
		dd->lbus_speed = 2500; /* Gen1, 2.5GHz */
		goto bail;
	}

	pos = pci_find_capability(dd->pcidev, PCI_CAP_ID_MSIX);
	if (nent && *nent && pos) {
		qib_msix_setup(dd, pos, nent, entry);
		ret = 0; /* did it, either MSIx or INTx */
	} else {
		pos = pci_find_capability(dd->pcidev, PCI_CAP_ID_MSI);
		if (pos)
			ret = qib_msi_setup(dd, pos);
		else
			qib_dev_err(dd, "No PCI MSI or MSIx capability!\n");
	}
	if (!pos)
		qib_enable_intx(dd->pcidev);

	pci_read_config_word(dd->pcidev, pose + PCI_EXP_LNKSTA, &linkstat);
	/*
	 * speed is bits 0-3, linkwidth is bits 4-8
	 * no defines for them in headers
	 */
	speed = linkstat & 0xf;
	linkstat >>= 4;
	linkstat &= 0x1f;
	dd->lbus_width = linkstat;

	switch (speed) {
	case 1:
		dd->lbus_speed = 2500; /* Gen1, 2.5GHz */
		break;
	case 2:
		dd->lbus_speed = 5000; /* Gen1, 5GHz */
		break;
	default: /* not defined, assume gen1 */
		dd->lbus_speed = 2500;
		break;
	}

	/*
	 * Check against expected pcie width and complain if "wrong"
	 * on first initialization, not afterwards (i.e., reset).
	 */
	if (minw && linkstat < minw)
		qib_dev_err(dd,
			    "PCIe width %u (x%u HCA), performance reduced\n",
			    linkstat, minw);

	qib_tune_pcie_caps(dd);

	qib_tune_pcie_coalesce(dd);

bail:
	/* fill in string, even on errors */
	snprintf(dd->lbus_info, sizeof(dd->lbus_info),
		 "PCIe,%uMHz,x%u\n", dd->lbus_speed, dd->lbus_width);
	return ret;
}

/*
 * Setup pcie interrupt stuff again after a reset.  I'd like to just call
 * pci_enable_msi() again for msi, but when I do that,
 * the MSI enable bit doesn't get set in the command word, and
 * we switch to to a different interrupt vector, which is confusing,
 * so I instead just do it all inline.  Perhaps somehow can tie this
 * into the PCIe hotplug support at some point
 */
int qib_reinit_intr(struct qib_devdata *dd)
{
	int pos;
	u16 control;
	int ret = 0;

	/* If we aren't using MSI, don't restore it */
	if (!dd->msi_lo)
		goto bail;

	pos = pci_find_capability(dd->pcidev, PCI_CAP_ID_MSI);
	if (!pos) {
		qib_dev_err(dd, "Can't find MSI capability, "
			    "can't restore MSI settings\n");
		ret = 0;
		/* nothing special for MSIx, just MSI */
		goto bail;
	}
	pci_write_config_dword(dd->pcidev, pos + PCI_MSI_ADDRESS_LO,
			       dd->msi_lo);
	pci_write_config_dword(dd->pcidev, pos + PCI_MSI_ADDRESS_HI,
			       dd->msi_hi);
	pci_read_config_word(dd->pcidev, pos + PCI_MSI_FLAGS, &control);
	if (!(control & PCI_MSI_FLAGS_ENABLE)) {
		control |= PCI_MSI_FLAGS_ENABLE;
		pci_write_config_word(dd->pcidev, pos + PCI_MSI_FLAGS,
				      control);
	}
	/* now rewrite the data (vector) info */
	pci_write_config_word(dd->pcidev, pos +
			      ((control & PCI_MSI_FLAGS_64BIT) ? 12 : 8),
			      dd->msi_data);
	ret = 1;
bail:
	if (!ret && (dd->flags & QIB_HAS_INTX)) {
		qib_enable_intx(dd->pcidev);
		ret = 1;
	}

	/* and now set the pci master bit again */
	pci_set_master(dd->pcidev);

	return ret;
}

/*
 * Disable msi interrupt if enabled, and clear msi_lo.
 * This is used primarily for the fallback to INTx, but
 * is also used in reinit after reset, and during cleanup.
 */
void qib_nomsi(struct qib_devdata *dd)
{
	dd->msi_lo = 0;
	pci_disable_msi(dd->pcidev);
}

/*
 * Same as qib_nosmi, but for MSIx.
 */
void qib_nomsix(struct qib_devdata *dd)
{
	pci_disable_msix(dd->pcidev);
}

/*
 * Similar to pci_intx(pdev, 1), except that we make sure
 * msi(x) is off.
 */
void qib_enable_intx(struct pci_dev *pdev)
{
	u16 cw, new;
	int pos;

	/* first, turn on INTx */
	pci_read_config_word(pdev, PCI_COMMAND, &cw);
	new = cw & ~PCI_COMMAND_INTX_DISABLE;
	if (new != cw)
		pci_write_config_word(pdev, PCI_COMMAND, new);

	pos = pci_find_capability(pdev, PCI_CAP_ID_MSI);
	if (pos) {
		/* then turn off MSI */
		pci_read_config_word(pdev, pos + PCI_MSI_FLAGS, &cw);
		new = cw & ~PCI_MSI_FLAGS_ENABLE;
		if (new != cw)
			pci_write_config_word(pdev, pos + PCI_MSI_FLAGS, new);
	}
	pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (pos) {
		/* then turn off MSIx */
		pci_read_config_word(pdev, pos + PCI_MSIX_FLAGS, &cw);
		new = cw & ~PCI_MSIX_FLAGS_ENABLE;
		if (new != cw)
			pci_write_config_word(pdev, pos + PCI_MSIX_FLAGS, new);
	}
}

/*
 * These two routines are helper routines for the device reset code
 * to move all the pcie code out of the chip-specific driver code.
 */
void qib_pcie_getcmd(struct qib_devdata *dd, u16 *cmd, u8 *iline, u8 *cline)
{
	pci_read_config_word(dd->pcidev, PCI_COMMAND, cmd);
	pci_read_config_byte(dd->pcidev, PCI_INTERRUPT_LINE, iline);
	pci_read_config_byte(dd->pcidev, PCI_CACHE_LINE_SIZE, cline);
}

void qib_pcie_reenable(struct qib_devdata *dd, u16 cmd, u8 iline, u8 cline)
{
	int r;
	r = pci_write_config_dword(dd->pcidev, PCI_BASE_ADDRESS_0,
				   dd->pcibar0);
	if (r)
		qib_dev_err(dd, "rewrite of BAR0 failed: %d\n", r);
	r = pci_write_config_dword(dd->pcidev, PCI_BASE_ADDRESS_1,
				   dd->pcibar1);
	if (r)
		qib_dev_err(dd, "rewrite of BAR1 failed: %d\n", r);
	/* now re-enable memory access, and restore cosmetic settings */
	pci_write_config_word(dd->pcidev, PCI_COMMAND, cmd);
	pci_write_config_byte(dd->pcidev, PCI_INTERRUPT_LINE, iline);
	pci_write_config_byte(dd->pcidev, PCI_CACHE_LINE_SIZE, cline);
	r = pci_enable_device(dd->pcidev);
	if (r)
		qib_dev_err(dd, "pci_enable_device failed after "
			    "reset: %d\n", r);
}

/* code to adjust PCIe capabilities. */

static int fld2val(int wd, int mask)
{
	int lsbmask;

	if (!mask)
		return 0;
	wd &= mask;
	lsbmask = mask ^ (mask & (mask - 1));
	wd /= lsbmask;
	return wd;
}

static int val2fld(int wd, int mask)
{
	int lsbmask;

	if (!mask)
		return 0;
	lsbmask = mask ^ (mask & (mask - 1));
	wd *= lsbmask;
	return wd;
}

static int qib_pcie_coalesce;
module_param_named(pcie_coalesce, qib_pcie_coalesce, int, S_IRUGO);
MODULE_PARM_DESC(pcie_coalesce, "tune PCIe colescing on some Intel chipsets");

/*
 * Enable PCIe completion and data coalescing, on Intel 5x00 and 7300
 * chipsets.   This is known to be unsafe for some revisions of some
 * of these chipsets, with some BIOS settings, and enabling it on those
 * systems may result in the system crashing, and/or data corruption.
 */
static int qib_tune_pcie_coalesce(struct qib_devdata *dd)
{
	int r;
	struct pci_dev *parent;
	int ppos;
	u16 devid;
	u32 mask, bits, val;

	if (!qib_pcie_coalesce)
		return 0;

	/* Find out supported and configured values for parent (root) */
	parent = dd->pcidev->bus->self;
	if (parent->bus->parent) {
		qib_devinfo(dd->pcidev, "Parent not root\n");
		return 1;
	}
	ppos = pci_pcie_cap(parent);
	if (!ppos)
		return 1;
	if (parent->vendor != 0x8086)
		return 1;

	/*
	 *  - bit 12: Max_rdcmp_Imt_EN: need to set to 1
	 *  - bit 11: COALESCE_FORCE: need to set to 0
	 *  - bit 10: COALESCE_EN: need to set to 1
	 *  (but limitations on some on some chipsets)
	 *
	 *  On the Intel 5000, 5100, and 7300 chipsets, there is
	 *  also: - bit 25:24: COALESCE_MODE, need to set to 0
	 */
	devid = parent->device;
	if (devid >= 0x25e2 && devid <= 0x25fa) {
		/* 5000 P/V/X/Z */
		if (parent->revision <= 0xb2)
			bits = 1U << 10;
		else
			bits = 7U << 10;
		mask = (3U << 24) | (7U << 10);
	} else if (devid >= 0x65e2 && devid <= 0x65fa) {
		/* 5100 */
		bits = 1U << 10;
		mask = (3U << 24) | (7U << 10);
	} else if (devid >= 0x4021 && devid <= 0x402e) {
		/* 5400 */
		bits = 7U << 10;
		mask = 7U << 10;
	} else if (devid >= 0x3604 && devid <= 0x360a) {
		/* 7300 */
		bits = 7U << 10;
		mask = (3U << 24) | (7U << 10);
	} else {
		/* not one of the chipsets that we know about */
		return 1;
	}
	pci_read_config_dword(parent, 0x48, &val);
	val &= ~mask;
	val |= bits;
	r = pci_write_config_dword(parent, 0x48, val);
	return 0;
}

/*
 * BIOS may not set PCIe bus-utilization parameters for best performance.
 * Check and optionally adjust them to maximize our throughput.
 */
static int qib_pcie_caps;
module_param_named(pcie_caps, qib_pcie_caps, int, S_IRUGO);
MODULE_PARM_DESC(pcie_caps, "Max PCIe tuning: Payload (0..3), ReadReq (4..7)");

static int qib_tune_pcie_caps(struct qib_devdata *dd)
{
	int ret = 1; /* Assume the worst */
	struct pci_dev *parent;
	int ppos, epos;
	u16 pcaps, pctl, ecaps, ectl;
	int rc_sup, ep_sup;
	int rc_cur, ep_cur;

	/* Find out supported and configured values for parent (root) */
	parent = dd->pcidev->bus->self;
	if (parent->bus->parent) {
		qib_devinfo(dd->pcidev, "Parent not root\n");
		goto bail;
	}
	ppos = pci_pcie_cap(parent);
	if (ppos) {
		pci_read_config_word(parent, ppos + PCI_EXP_DEVCAP, &pcaps);
		pci_read_config_word(parent, ppos + PCI_EXP_DEVCTL, &pctl);
	} else
		goto bail;
	/* Find out supported and configured values for endpoint (us) */
	epos = pci_pcie_cap(dd->pcidev);
	if (epos) {
		pci_read_config_word(dd->pcidev, epos + PCI_EXP_DEVCAP, &ecaps);
		pci_read_config_word(dd->pcidev, epos + PCI_EXP_DEVCTL, &ectl);
	} else
		goto bail;
	ret = 0;
	/* Find max payload supported by root, endpoint */
	rc_sup = fld2val(pcaps, PCI_EXP_DEVCAP_PAYLOAD);
	ep_sup = fld2val(ecaps, PCI_EXP_DEVCAP_PAYLOAD);
	if (rc_sup > ep_sup)
		rc_sup = ep_sup;

	rc_cur = fld2val(pctl, PCI_EXP_DEVCTL_PAYLOAD);
	ep_cur = fld2val(ectl, PCI_EXP_DEVCTL_PAYLOAD);

	/* If Supported greater than limit in module param, limit it */
	if (rc_sup > (qib_pcie_caps & 7))
		rc_sup = qib_pcie_caps & 7;
	/* If less than (allowed, supported), bump root payload */
	if (rc_sup > rc_cur) {
		rc_cur = rc_sup;
		pctl = (pctl & ~PCI_EXP_DEVCTL_PAYLOAD) |
			val2fld(rc_cur, PCI_EXP_DEVCTL_PAYLOAD);
		pci_write_config_word(parent, ppos + PCI_EXP_DEVCTL, pctl);
	}
	/* If less than (allowed, supported), bump endpoint payload */
	if (rc_sup > ep_cur) {
		ep_cur = rc_sup;
		ectl = (ectl & ~PCI_EXP_DEVCTL_PAYLOAD) |
			val2fld(ep_cur, PCI_EXP_DEVCTL_PAYLOAD);
		pci_write_config_word(dd->pcidev, epos + PCI_EXP_DEVCTL, ectl);
	}

	/*
	 * Now the Read Request size.
	 * No field for max supported, but PCIe spec limits it to 4096,
	 * which is code '5' (log2(4096) - 7)
	 */
	rc_sup = 5;
	if (rc_sup > ((qib_pcie_caps >> 4) & 7))
		rc_sup = (qib_pcie_caps >> 4) & 7;
	rc_cur = fld2val(pctl, PCI_EXP_DEVCTL_READRQ);
	ep_cur = fld2val(ectl, PCI_EXP_DEVCTL_READRQ);

	if (rc_sup > rc_cur) {
		rc_cur = rc_sup;
		pctl = (pctl & ~PCI_EXP_DEVCTL_READRQ) |
			val2fld(rc_cur, PCI_EXP_DEVCTL_READRQ);
		pci_write_config_word(parent, ppos + PCI_EXP_DEVCTL, pctl);
	}
	if (rc_sup > ep_cur) {
		ep_cur = rc_sup;
		ectl = (ectl & ~PCI_EXP_DEVCTL_READRQ) |
			val2fld(ep_cur, PCI_EXP_DEVCTL_READRQ);
		pci_write_config_word(dd->pcidev, epos + PCI_EXP_DEVCTL, ectl);
	}
bail:
	return ret;
}
/* End of PCIe capability tuning */

/*
 * From here through qib_pci_err_handler definition is invoked via
 * PCI error infrastructure, registered via pci
 */
static pci_ers_result_t
qib_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct qib_devdata *dd = pci_get_drvdata(pdev);
	pci_ers_result_t ret = PCI_ERS_RESULT_RECOVERED;

	switch (state) {
	case pci_channel_io_normal:
		qib_devinfo(pdev, "State Normal, ignoring\n");
		break;

	case pci_channel_io_frozen:
		qib_devinfo(pdev, "State Frozen, requesting reset\n");
		pci_disable_device(pdev);
		ret = PCI_ERS_RESULT_NEED_RESET;
		break;

	case pci_channel_io_perm_failure:
		qib_devinfo(pdev, "State Permanent Failure, disabling\n");
		if (dd) {
			/* no more register accesses! */
			dd->flags &= ~QIB_PRESENT;
			qib_disable_after_error(dd);
		}
		 /* else early, or other problem */
		ret =  PCI_ERS_RESULT_DISCONNECT;
		break;

	default: /* shouldn't happen */
		qib_devinfo(pdev, "QIB PCI errors detected (state %d)\n",
			state);
		break;
	}
	return ret;
}

static pci_ers_result_t
qib_pci_mmio_enabled(struct pci_dev *pdev)
{
	u64 words = 0U;
	struct qib_devdata *dd = pci_get_drvdata(pdev);
	pci_ers_result_t ret = PCI_ERS_RESULT_RECOVERED;

	if (dd && dd->pport) {
		words = dd->f_portcntr(dd->pport, QIBPORTCNTR_WORDRCV);
		if (words == ~0ULL)
			ret = PCI_ERS_RESULT_NEED_RESET;
	}
	qib_devinfo(pdev, "QIB mmio_enabled function called, "
		 "read wordscntr %Lx, returning %d\n", words, ret);
	return  ret;
}

static pci_ers_result_t
qib_pci_slot_reset(struct pci_dev *pdev)
{
	qib_devinfo(pdev, "QIB link_reset function called, ignored\n");
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static pci_ers_result_t
qib_pci_link_reset(struct pci_dev *pdev)
{
	qib_devinfo(pdev, "QIB link_reset function called, ignored\n");
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static void
qib_pci_resume(struct pci_dev *pdev)
{
	struct qib_devdata *dd = pci_get_drvdata(pdev);
	qib_devinfo(pdev, "QIB resume function called\n");
	pci_cleanup_aer_uncorrect_error_status(pdev);
	/*
	 * Running jobs will fail, since it's asynchronous
	 * unlike sysfs-requested reset.   Better than
	 * doing nothing.
	 */
	qib_init(dd, 1); /* same as re-init after reset */
}

struct pci_error_handlers qib_pci_err_handler = {
	.error_detected = qib_pci_error_detected,
	.mmio_enabled = qib_pci_mmio_enabled,
	.link_reset = qib_pci_link_reset,
	.slot_reset = qib_pci_slot_reset,
	.resume = qib_pci_resume,
};
