/*
 * Copyright(c) 2015 - 2019 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/aer.h>
#include <linux/module.h>

#include "hfi.h"
#include "chip_registers.h"
#include "aspm.h"

/*
 * This file contains PCIe utility routines.
 */

/*
 * Do all the common PCIe setup and initialization.
 */
int hfi1_pcie_init(struct hfi1_devdata *dd)
{
	int ret;
	struct pci_dev *pdev = dd->pcidev;

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
		dd_dev_err(dd, "pci enable failed: error %d\n", -ret);
		return ret;
	}

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret) {
		dd_dev_err(dd, "pci_request_regions fails: err %d\n", -ret);
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
			dd_dev_err(dd, "Unable to set DMA mask: %d\n", ret);
			goto bail;
		}
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	} else {
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	}
	if (ret) {
		dd_dev_err(dd, "Unable to set DMA consistent mask: %d\n", ret);
		goto bail;
	}

	pci_set_master(pdev);
	(void)pci_enable_pcie_error_reporting(pdev);
	return 0;

bail:
	hfi1_pcie_cleanup(pdev);
	return ret;
}

/*
 * Clean what was done in hfi1_pcie_init()
 */
void hfi1_pcie_cleanup(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
	/*
	 * Release regions should be called after the disable. OK to
	 * call if request regions has not been called or failed.
	 */
	pci_release_regions(pdev);
}

/*
 * Do remaining PCIe setup, once dd is allocated, and save away
 * fields required to re-initialize after a chip reset, or for
 * various other purposes
 */
int hfi1_pcie_ddinit(struct hfi1_devdata *dd, struct pci_dev *pdev)
{
	unsigned long len;
	resource_size_t addr;
	int ret = 0;
	u32 rcv_array_count;

	addr = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);

	/*
	 * The TXE PIO buffers are at the tail end of the chip space.
	 * Cut them off and map them separately.
	 */

	/* sanity check vs expectations */
	if (len != TXE_PIO_SEND + TXE_PIO_SIZE) {
		dd_dev_err(dd, "chip PIO range does not match\n");
		return -EINVAL;
	}

	dd->kregbase1 = ioremap_nocache(addr, RCV_ARRAY);
	if (!dd->kregbase1) {
		dd_dev_err(dd, "UC mapping of kregbase1 failed\n");
		return -ENOMEM;
	}
	dd_dev_info(dd, "UC base1: %p for %x\n", dd->kregbase1, RCV_ARRAY);

	/* verify that reads actually work, save revision for reset check */
	dd->revision = readq(dd->kregbase1 + CCE_REVISION);
	if (dd->revision == ~(u64)0) {
		dd_dev_err(dd, "Cannot read chip CSRs\n");
		goto nomem;
	}

	rcv_array_count = readq(dd->kregbase1 + RCV_ARRAY_CNT);
	dd_dev_info(dd, "RcvArray count: %u\n", rcv_array_count);
	dd->base2_start  = RCV_ARRAY + rcv_array_count * 8;

	dd->kregbase2 = ioremap_nocache(
		addr + dd->base2_start,
		TXE_PIO_SEND - dd->base2_start);
	if (!dd->kregbase2) {
		dd_dev_err(dd, "UC mapping of kregbase2 failed\n");
		goto nomem;
	}
	dd_dev_info(dd, "UC base2: %p for %x\n", dd->kregbase2,
		    TXE_PIO_SEND - dd->base2_start);

	dd->piobase = ioremap_wc(addr + TXE_PIO_SEND, TXE_PIO_SIZE);
	if (!dd->piobase) {
		dd_dev_err(dd, "WC mapping of send buffers failed\n");
		goto nomem;
	}
	dd_dev_info(dd, "WC piobase: %p for %x\n", dd->piobase, TXE_PIO_SIZE);

	dd->physaddr = addr;        /* used for io_remap, etc. */

	/*
	 * Map the chip's RcvArray as write-combining to allow us
	 * to write an entire cacheline worth of entries in one shot.
	 */
	dd->rcvarray_wc = ioremap_wc(addr + RCV_ARRAY,
				     rcv_array_count * 8);
	if (!dd->rcvarray_wc) {
		dd_dev_err(dd, "WC mapping of receive array failed\n");
		goto nomem;
	}
	dd_dev_info(dd, "WC RcvArray: %p for %x\n",
		    dd->rcvarray_wc, rcv_array_count * 8);

	dd->flags |= HFI1_PRESENT;	/* chip.c CSR routines now work */
	return 0;
nomem:
	ret = -ENOMEM;
	hfi1_pcie_ddcleanup(dd);
	return ret;
}

/*
 * Do PCIe cleanup related to dd, after chip-specific cleanup, etc.  Just prior
 * to releasing the dd memory.
 * Void because all of the core pcie cleanup functions are void.
 */
void hfi1_pcie_ddcleanup(struct hfi1_devdata *dd)
{
	dd->flags &= ~HFI1_PRESENT;
	if (dd->kregbase1)
		iounmap(dd->kregbase1);
	dd->kregbase1 = NULL;
	if (dd->kregbase2)
		iounmap(dd->kregbase2);
	dd->kregbase2 = NULL;
	if (dd->rcvarray_wc)
		iounmap(dd->rcvarray_wc);
	dd->rcvarray_wc = NULL;
	if (dd->piobase)
		iounmap(dd->piobase);
	dd->piobase = NULL;
}

/* return the PCIe link speed from the given link status */
static u32 extract_speed(u16 linkstat)
{
	u32 speed;

	switch (linkstat & PCI_EXP_LNKSTA_CLS) {
	default: /* not defined, assume Gen1 */
	case PCI_EXP_LNKSTA_CLS_2_5GB:
		speed = 2500; /* Gen 1, 2.5GHz */
		break;
	case PCI_EXP_LNKSTA_CLS_5_0GB:
		speed = 5000; /* Gen 2, 5GHz */
		break;
	case PCI_EXP_LNKSTA_CLS_8_0GB:
		speed = 8000; /* Gen 3, 8GHz */
		break;
	}
	return speed;
}

/* return the PCIe link speed from the given link status */
static u32 extract_width(u16 linkstat)
{
	return (linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
}

/* read the link status and set dd->{lbus_width,lbus_speed,lbus_info} */
static void update_lbus_info(struct hfi1_devdata *dd)
{
	u16 linkstat;
	int ret;

	ret = pcie_capability_read_word(dd->pcidev, PCI_EXP_LNKSTA, &linkstat);
	if (ret) {
		dd_dev_err(dd, "Unable to read from PCI config\n");
		return;
	}

	dd->lbus_width = extract_width(linkstat);
	dd->lbus_speed = extract_speed(linkstat);
	snprintf(dd->lbus_info, sizeof(dd->lbus_info),
		 "PCIe,%uMHz,x%u", dd->lbus_speed, dd->lbus_width);
}

/*
 * Read in the current PCIe link width and speed.  Find if the link is
 * Gen3 capable.
 */
int pcie_speeds(struct hfi1_devdata *dd)
{
	u32 linkcap;
	struct pci_dev *parent = dd->pcidev->bus->self;
	int ret;

	if (!pci_is_pcie(dd->pcidev)) {
		dd_dev_err(dd, "Can't find PCI Express capability!\n");
		return -EINVAL;
	}

	/* find if our max speed is Gen3 and parent supports Gen3 speeds */
	dd->link_gen3_capable = 1;

	ret = pcie_capability_read_dword(dd->pcidev, PCI_EXP_LNKCAP, &linkcap);
	if (ret) {
		dd_dev_err(dd, "Unable to read from PCI config\n");
		return ret;
	}

	if ((linkcap & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_8_0GB) {
		dd_dev_info(dd,
			    "This HFI is not Gen3 capable, max speed 0x%x, need 0x3\n",
			    linkcap & PCI_EXP_LNKCAP_SLS);
		dd->link_gen3_capable = 0;
	}

	/*
	 * bus->max_bus_speed is set from the bridge's linkcap Max Link Speed
	 */
	if (parent && dd->pcidev->bus->max_bus_speed != PCIE_SPEED_8_0GT) {
		dd_dev_info(dd, "Parent PCIe bridge does not support Gen3\n");
		dd->link_gen3_capable = 0;
	}

	/* obtain the link width and current speed */
	update_lbus_info(dd);

	dd_dev_info(dd, "%s\n", dd->lbus_info);

	return 0;
}

/* restore command and BARs after a reset has wiped them out */
int restore_pci_variables(struct hfi1_devdata *dd)
{
	int ret = 0;

	ret = pci_write_config_word(dd->pcidev, PCI_COMMAND, dd->pci_command);
	if (ret)
		goto error;

	ret = pci_write_config_dword(dd->pcidev, PCI_BASE_ADDRESS_0,
				     dd->pcibar0);
	if (ret)
		goto error;

	ret = pci_write_config_dword(dd->pcidev, PCI_BASE_ADDRESS_1,
				     dd->pcibar1);
	if (ret)
		goto error;

	ret = pci_write_config_dword(dd->pcidev, PCI_ROM_ADDRESS, dd->pci_rom);
	if (ret)
		goto error;

	ret = pcie_capability_write_word(dd->pcidev, PCI_EXP_DEVCTL,
					 dd->pcie_devctl);
	if (ret)
		goto error;

	ret = pcie_capability_write_word(dd->pcidev, PCI_EXP_LNKCTL,
					 dd->pcie_lnkctl);
	if (ret)
		goto error;

	ret = pcie_capability_write_word(dd->pcidev, PCI_EXP_DEVCTL2,
					 dd->pcie_devctl2);
	if (ret)
		goto error;

	ret = pci_write_config_dword(dd->pcidev, PCI_CFG_MSIX0, dd->pci_msix0);
	if (ret)
		goto error;

	if (pci_find_ext_capability(dd->pcidev, PCI_EXT_CAP_ID_TPH)) {
		ret = pci_write_config_dword(dd->pcidev, PCIE_CFG_TPH2,
					     dd->pci_tph2);
		if (ret)
			goto error;
	}
	return 0;

error:
	dd_dev_err(dd, "Unable to write to PCI config\n");
	return ret;
}

/* Save BARs and command to rewrite after device reset */
int save_pci_variables(struct hfi1_devdata *dd)
{
	int ret = 0;

	ret = pci_read_config_dword(dd->pcidev, PCI_BASE_ADDRESS_0,
				    &dd->pcibar0);
	if (ret)
		goto error;

	ret = pci_read_config_dword(dd->pcidev, PCI_BASE_ADDRESS_1,
				    &dd->pcibar1);
	if (ret)
		goto error;

	ret = pci_read_config_dword(dd->pcidev, PCI_ROM_ADDRESS, &dd->pci_rom);
	if (ret)
		goto error;

	ret = pci_read_config_word(dd->pcidev, PCI_COMMAND, &dd->pci_command);
	if (ret)
		goto error;

	ret = pcie_capability_read_word(dd->pcidev, PCI_EXP_DEVCTL,
					&dd->pcie_devctl);
	if (ret)
		goto error;

	ret = pcie_capability_read_word(dd->pcidev, PCI_EXP_LNKCTL,
					&dd->pcie_lnkctl);
	if (ret)
		goto error;

	ret = pcie_capability_read_word(dd->pcidev, PCI_EXP_DEVCTL2,
					&dd->pcie_devctl2);
	if (ret)
		goto error;

	ret = pci_read_config_dword(dd->pcidev, PCI_CFG_MSIX0, &dd->pci_msix0);
	if (ret)
		goto error;

	if (pci_find_ext_capability(dd->pcidev, PCI_EXT_CAP_ID_TPH)) {
		ret = pci_read_config_dword(dd->pcidev, PCIE_CFG_TPH2,
					    &dd->pci_tph2);
		if (ret)
			goto error;
	}
	return 0;

error:
	dd_dev_err(dd, "Unable to read from PCI config\n");
	return ret;
}

/*
 * BIOS may not set PCIe bus-utilization parameters for best performance.
 * Check and optionally adjust them to maximize our throughput.
 */
static int hfi1_pcie_caps;
module_param_named(pcie_caps, hfi1_pcie_caps, int, 0444);
MODULE_PARM_DESC(pcie_caps, "Max PCIe tuning: Payload (0..3), ReadReq (4..7)");

/**
 * tune_pcie_caps() - Code to adjust PCIe capabilities.
 * @dd: Valid device data structure
 *
 */
void tune_pcie_caps(struct hfi1_devdata *dd)
{
	struct pci_dev *parent;
	u16 rc_mpss, rc_mps, ep_mpss, ep_mps;
	u16 rc_mrrs, ep_mrrs, max_mrrs, ectl;
	int ret;

	/*
	 * Turn on extended tags in DevCtl in case the BIOS has turned it off
	 * to improve WFR SDMA bandwidth
	 */
	ret = pcie_capability_read_word(dd->pcidev, PCI_EXP_DEVCTL, &ectl);
	if ((!ret) && !(ectl & PCI_EXP_DEVCTL_EXT_TAG)) {
		dd_dev_info(dd, "Enabling PCIe extended tags\n");
		ectl |= PCI_EXP_DEVCTL_EXT_TAG;
		ret = pcie_capability_write_word(dd->pcidev,
						 PCI_EXP_DEVCTL, ectl);
		if (ret)
			dd_dev_info(dd, "Unable to write to PCI config\n");
	}
	/* Find out supported and configured values for parent (root) */
	parent = dd->pcidev->bus->self;
	/*
	 * The driver cannot perform the tuning if it does not have
	 * access to the upstream component.
	 */
	if (!parent) {
		dd_dev_info(dd, "Parent not found\n");
		return;
	}
	if (!pci_is_root_bus(parent->bus)) {
		dd_dev_info(dd, "Parent not root\n");
		return;
	}
	if (!pci_is_pcie(parent)) {
		dd_dev_info(dd, "Parent is not PCI Express capable\n");
		return;
	}
	if (!pci_is_pcie(dd->pcidev)) {
		dd_dev_info(dd, "PCI device is not PCI Express capable\n");
		return;
	}
	rc_mpss = parent->pcie_mpss;
	rc_mps = ffs(pcie_get_mps(parent)) - 8;
	/* Find out supported and configured values for endpoint (us) */
	ep_mpss = dd->pcidev->pcie_mpss;
	ep_mps = ffs(pcie_get_mps(dd->pcidev)) - 8;

	/* Find max payload supported by root, endpoint */
	if (rc_mpss > ep_mpss)
		rc_mpss = ep_mpss;

	/* If Supported greater than limit in module param, limit it */
	if (rc_mpss > (hfi1_pcie_caps & 7))
		rc_mpss = hfi1_pcie_caps & 7;
	/* If less than (allowed, supported), bump root payload */
	if (rc_mpss > rc_mps) {
		rc_mps = rc_mpss;
		pcie_set_mps(parent, 128 << rc_mps);
	}
	/* If less than (allowed, supported), bump endpoint payload */
	if (rc_mpss > ep_mps) {
		ep_mps = rc_mpss;
		pcie_set_mps(dd->pcidev, 128 << ep_mps);
	}

	/*
	 * Now the Read Request size.
	 * No field for max supported, but PCIe spec limits it to 4096,
	 * which is code '5' (log2(4096) - 7)
	 */
	max_mrrs = 5;
	if (max_mrrs > ((hfi1_pcie_caps >> 4) & 7))
		max_mrrs = (hfi1_pcie_caps >> 4) & 7;

	max_mrrs = 128 << max_mrrs;
	rc_mrrs = pcie_get_readrq(parent);
	ep_mrrs = pcie_get_readrq(dd->pcidev);

	if (max_mrrs > rc_mrrs) {
		rc_mrrs = max_mrrs;
		pcie_set_readrq(parent, rc_mrrs);
	}
	if (max_mrrs > ep_mrrs) {
		ep_mrrs = max_mrrs;
		pcie_set_readrq(dd->pcidev, ep_mrrs);
	}
}

/* End of PCIe capability tuning */

/*
 * From here through hfi1_pci_err_handler definition is invoked via
 * PCI error infrastructure, registered via pci
 */
static pci_ers_result_t
pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct hfi1_devdata *dd = pci_get_drvdata(pdev);
	pci_ers_result_t ret = PCI_ERS_RESULT_RECOVERED;

	switch (state) {
	case pci_channel_io_normal:
		dd_dev_info(dd, "State Normal, ignoring\n");
		break;

	case pci_channel_io_frozen:
		dd_dev_info(dd, "State Frozen, requesting reset\n");
		pci_disable_device(pdev);
		ret = PCI_ERS_RESULT_NEED_RESET;
		break;

	case pci_channel_io_perm_failure:
		if (dd) {
			dd_dev_info(dd, "State Permanent Failure, disabling\n");
			/* no more register accesses! */
			dd->flags &= ~HFI1_PRESENT;
			hfi1_disable_after_error(dd);
		}
		 /* else early, or other problem */
		ret =  PCI_ERS_RESULT_DISCONNECT;
		break;

	default: /* shouldn't happen */
		dd_dev_info(dd, "HFI1 PCI errors detected (state %d)\n",
			    state);
		break;
	}
	return ret;
}

static pci_ers_result_t
pci_mmio_enabled(struct pci_dev *pdev)
{
	u64 words = 0U;
	struct hfi1_devdata *dd = pci_get_drvdata(pdev);
	pci_ers_result_t ret = PCI_ERS_RESULT_RECOVERED;

	if (dd && dd->pport) {
		words = read_port_cntr(dd->pport, C_RX_WORDS, CNTR_INVALID_VL);
		if (words == ~0ULL)
			ret = PCI_ERS_RESULT_NEED_RESET;
		dd_dev_info(dd,
			    "HFI1 mmio_enabled function called, read wordscntr %llx, returning %d\n",
			    words, ret);
	}
	return  ret;
}

static pci_ers_result_t
pci_slot_reset(struct pci_dev *pdev)
{
	struct hfi1_devdata *dd = pci_get_drvdata(pdev);

	dd_dev_info(dd, "HFI1 slot_reset function called, ignored\n");
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static void
pci_resume(struct pci_dev *pdev)
{
	struct hfi1_devdata *dd = pci_get_drvdata(pdev);

	dd_dev_info(dd, "HFI1 resume function called\n");
	/*
	 * Running jobs will fail, since it's asynchronous
	 * unlike sysfs-requested reset.   Better than
	 * doing nothing.
	 */
	hfi1_init(dd, 1); /* same as re-init after reset */
}

const struct pci_error_handlers hfi1_pci_err_handler = {
	.error_detected = pci_error_detected,
	.mmio_enabled = pci_mmio_enabled,
	.slot_reset = pci_slot_reset,
	.resume = pci_resume,
};

/*============================================================================*/
/* PCIe Gen3 support */

/*
 * This code is separated out because it is expected to be removed in the
 * final shipping product.  If not, then it will be revisited and items
 * will be moved to more standard locations.
 */

/* ASIC_PCI_SD_HOST_STATUS.FW_DNLD_STS field values */
#define DL_STATUS_HFI0 0x1	/* hfi0 firmware download complete */
#define DL_STATUS_HFI1 0x2	/* hfi1 firmware download complete */
#define DL_STATUS_BOTH 0x3	/* hfi0 and hfi1 firmware download complete */

/* ASIC_PCI_SD_HOST_STATUS.FW_DNLD_ERR field values */
#define DL_ERR_NONE		0x0	/* no error */
#define DL_ERR_SWAP_PARITY	0x1	/* parity error in SerDes interrupt */
					/*   or response data */
#define DL_ERR_DISABLED	0x2	/* hfi disabled */
#define DL_ERR_SECURITY	0x3	/* security check failed */
#define DL_ERR_SBUS		0x4	/* SBus status error */
#define DL_ERR_XFR_PARITY	0x5	/* parity error during ROM transfer*/

/* gasket block secondary bus reset delay */
#define SBR_DELAY_US 200000	/* 200ms */

static uint pcie_target = 3;
module_param(pcie_target, uint, S_IRUGO);
MODULE_PARM_DESC(pcie_target, "PCIe target speed (0 skip, 1-3 Gen1-3)");

static uint pcie_force;
module_param(pcie_force, uint, S_IRUGO);
MODULE_PARM_DESC(pcie_force, "Force driver to do a PCIe firmware download even if already at target speed");

static uint pcie_retry = 5;
module_param(pcie_retry, uint, S_IRUGO);
MODULE_PARM_DESC(pcie_retry, "Driver will try this many times to reach requested speed");

#define UNSET_PSET 255
#define DEFAULT_DISCRETE_PSET 2	/* discrete HFI */
#define DEFAULT_MCP_PSET 6	/* MCP HFI */
static uint pcie_pset = UNSET_PSET;
module_param(pcie_pset, uint, S_IRUGO);
MODULE_PARM_DESC(pcie_pset, "PCIe Eq Pset value to use, range is 0-10");

static uint pcie_ctle = 3; /* discrete on, integrated on */
module_param(pcie_ctle, uint, S_IRUGO);
MODULE_PARM_DESC(pcie_ctle, "PCIe static CTLE mode, bit 0 - discrete on/off, bit 1 - integrated on/off");

/* equalization columns */
#define PREC 0
#define ATTN 1
#define POST 2

/* discrete silicon preliminary equalization values */
static const u8 discrete_preliminary_eq[11][3] = {
	/* prec   attn   post */
	{  0x00,  0x00,  0x12 },	/* p0 */
	{  0x00,  0x00,  0x0c },	/* p1 */
	{  0x00,  0x00,  0x0f },	/* p2 */
	{  0x00,  0x00,  0x09 },	/* p3 */
	{  0x00,  0x00,  0x00 },	/* p4 */
	{  0x06,  0x00,  0x00 },	/* p5 */
	{  0x09,  0x00,  0x00 },	/* p6 */
	{  0x06,  0x00,  0x0f },	/* p7 */
	{  0x09,  0x00,  0x09 },	/* p8 */
	{  0x0c,  0x00,  0x00 },	/* p9 */
	{  0x00,  0x00,  0x18 },	/* p10 */
};

/* integrated silicon preliminary equalization values */
static const u8 integrated_preliminary_eq[11][3] = {
	/* prec   attn   post */
	{  0x00,  0x1e,  0x07 },	/* p0 */
	{  0x00,  0x1e,  0x05 },	/* p1 */
	{  0x00,  0x1e,  0x06 },	/* p2 */
	{  0x00,  0x1e,  0x04 },	/* p3 */
	{  0x00,  0x1e,  0x00 },	/* p4 */
	{  0x03,  0x1e,  0x00 },	/* p5 */
	{  0x04,  0x1e,  0x00 },	/* p6 */
	{  0x03,  0x1e,  0x06 },	/* p7 */
	{  0x03,  0x1e,  0x04 },	/* p8 */
	{  0x05,  0x1e,  0x00 },	/* p9 */
	{  0x00,  0x1e,  0x0a },	/* p10 */
};

static const u8 discrete_ctle_tunings[11][4] = {
	/* DC     LF     HF     BW */
	{  0x48,  0x0b,  0x04,  0x04 },	/* p0 */
	{  0x60,  0x05,  0x0f,  0x0a },	/* p1 */
	{  0x50,  0x09,  0x06,  0x06 },	/* p2 */
	{  0x68,  0x05,  0x0f,  0x0a },	/* p3 */
	{  0x80,  0x05,  0x0f,  0x0a },	/* p4 */
	{  0x70,  0x05,  0x0f,  0x0a },	/* p5 */
	{  0x68,  0x05,  0x0f,  0x0a },	/* p6 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p7 */
	{  0x48,  0x09,  0x06,  0x06 },	/* p8 */
	{  0x60,  0x05,  0x0f,  0x0a },	/* p9 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p10 */
};

static const u8 integrated_ctle_tunings[11][4] = {
	/* DC     LF     HF     BW */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p0 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p1 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p2 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p3 */
	{  0x58,  0x0a,  0x05,  0x05 },	/* p4 */
	{  0x48,  0x0a,  0x05,  0x05 },	/* p5 */
	{  0x40,  0x0a,  0x05,  0x05 },	/* p6 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p7 */
	{  0x38,  0x0f,  0x00,  0x00 },	/* p8 */
	{  0x38,  0x09,  0x06,  0x06 },	/* p9 */
	{  0x38,  0x0e,  0x01,  0x01 },	/* p10 */
};

/* helper to format the value to write to hardware */
#define eq_value(pre, curr, post) \
	((((u32)(pre)) << \
			PCIE_CFG_REG_PL102_GEN3_EQ_PRE_CURSOR_PSET_SHIFT) \
	| (((u32)(curr)) << PCIE_CFG_REG_PL102_GEN3_EQ_CURSOR_PSET_SHIFT) \
	| (((u32)(post)) << \
		PCIE_CFG_REG_PL102_GEN3_EQ_POST_CURSOR_PSET_SHIFT))

/*
 * Load the given EQ preset table into the PCIe hardware.
 */
static int load_eq_table(struct hfi1_devdata *dd, const u8 eq[11][3], u8 fs,
			 u8 div)
{
	struct pci_dev *pdev = dd->pcidev;
	u32 hit_error = 0;
	u32 violation;
	u32 i;
	u8 c_minus1, c0, c_plus1;
	int ret;

	for (i = 0; i < 11; i++) {
		/* set index */
		pci_write_config_dword(pdev, PCIE_CFG_REG_PL103, i);
		/* write the value */
		c_minus1 = eq[i][PREC] / div;
		c0 = fs - (eq[i][PREC] / div) - (eq[i][POST] / div);
		c_plus1 = eq[i][POST] / div;
		pci_write_config_dword(pdev, PCIE_CFG_REG_PL102,
				       eq_value(c_minus1, c0, c_plus1));
		/* check if these coefficients violate EQ rules */
		ret = pci_read_config_dword(dd->pcidev,
					    PCIE_CFG_REG_PL105, &violation);
		if (ret) {
			dd_dev_err(dd, "Unable to read from PCI config\n");
			hit_error = 1;
			break;
		}

		if (violation
		    & PCIE_CFG_REG_PL105_GEN3_EQ_VIOLATE_COEF_RULES_SMASK){
			if (hit_error == 0) {
				dd_dev_err(dd,
					   "Gen3 EQ Table Coefficient rule violations\n");
				dd_dev_err(dd, "         prec   attn   post\n");
			}
			dd_dev_err(dd, "   p%02d:   %02x     %02x     %02x\n",
				   i, (u32)eq[i][0], (u32)eq[i][1],
				   (u32)eq[i][2]);
			dd_dev_err(dd, "            %02x     %02x     %02x\n",
				   (u32)c_minus1, (u32)c0, (u32)c_plus1);
			hit_error = 1;
		}
	}
	if (hit_error)
		return -EINVAL;
	return 0;
}

/*
 * Steps to be done after the PCIe firmware is downloaded and
 * before the SBR for the Pcie Gen3.
 * The SBus resource is already being held.
 */
static void pcie_post_steps(struct hfi1_devdata *dd)
{
	int i;

	set_sbus_fast_mode(dd);
	/*
	 * Write to the PCIe PCSes to set the G3_LOCKED_NEXT bits to 1.
	 * This avoids a spurious framing error that can otherwise be
	 * generated by the MAC layer.
	 *
	 * Use individual addresses since no broadcast is set up.
	 */
	for (i = 0; i < NUM_PCIE_SERDES; i++) {
		sbus_request(dd, pcie_pcs_addrs[dd->hfi1_id][i],
			     0x03, WRITE_SBUS_RECEIVER, 0x00022132);
	}

	clear_sbus_fast_mode(dd);
}

/*
 * Trigger a secondary bus reset (SBR) on ourselves using our parent.
 *
 * Based on pci_parent_bus_reset() which is not exported by the
 * kernel core.
 */
static int trigger_sbr(struct hfi1_devdata *dd)
{
	struct pci_dev *dev = dd->pcidev;
	struct pci_dev *pdev;

	/* need a parent */
	if (!dev->bus->self) {
		dd_dev_err(dd, "%s: no parent device\n", __func__);
		return -ENOTTY;
	}

	/* should not be anyone else on the bus */
	list_for_each_entry(pdev, &dev->bus->devices, bus_list)
		if (pdev != dev) {
			dd_dev_err(dd,
				   "%s: another device is on the same bus\n",
				   __func__);
			return -ENOTTY;
		}

	/*
	 * This is an end around to do an SBR during probe time. A new API needs
	 * to be implemented to have cleaner interface but this fixes the
	 * current brokenness
	 */
	return pci_bridge_secondary_bus_reset(dev->bus->self);
}

/*
 * Write the given gasket interrupt register.
 */
static void write_gasket_interrupt(struct hfi1_devdata *dd, int index,
				   u16 code, u16 data)
{
	write_csr(dd, ASIC_PCIE_SD_INTRPT_LIST + (index * 8),
		  (((u64)code << ASIC_PCIE_SD_INTRPT_LIST_INTRPT_CODE_SHIFT) |
		   ((u64)data << ASIC_PCIE_SD_INTRPT_LIST_INTRPT_DATA_SHIFT)));
}

/*
 * Tell the gasket logic how to react to the reset.
 */
static void arm_gasket_logic(struct hfi1_devdata *dd)
{
	u64 reg;

	reg = (((u64)1 << dd->hfi1_id) <<
	       ASIC_PCIE_SD_HOST_CMD_INTRPT_CMD_SHIFT) |
	      ((u64)pcie_serdes_broadcast[dd->hfi1_id] <<
	       ASIC_PCIE_SD_HOST_CMD_SBUS_RCVR_ADDR_SHIFT |
	       ASIC_PCIE_SD_HOST_CMD_SBR_MODE_SMASK |
	       ((u64)SBR_DELAY_US & ASIC_PCIE_SD_HOST_CMD_TIMER_MASK) <<
	       ASIC_PCIE_SD_HOST_CMD_TIMER_SHIFT);
	write_csr(dd, ASIC_PCIE_SD_HOST_CMD, reg);
	/* read back to push the write */
	read_csr(dd, ASIC_PCIE_SD_HOST_CMD);
}

/*
 * CCE_PCIE_CTRL long name helpers
 * We redefine these shorter macros to use in the code while leaving
 * chip_registers.h to be autogenerated from the hardware spec.
 */
#define LANE_BUNDLE_MASK              CCE_PCIE_CTRL_PCIE_LANE_BUNDLE_MASK
#define LANE_BUNDLE_SHIFT             CCE_PCIE_CTRL_PCIE_LANE_BUNDLE_SHIFT
#define LANE_DELAY_MASK               CCE_PCIE_CTRL_PCIE_LANE_DELAY_MASK
#define LANE_DELAY_SHIFT              CCE_PCIE_CTRL_PCIE_LANE_DELAY_SHIFT
#define MARGIN_OVERWRITE_ENABLE_SHIFT CCE_PCIE_CTRL_XMT_MARGIN_OVERWRITE_ENABLE_SHIFT
#define MARGIN_SHIFT                  CCE_PCIE_CTRL_XMT_MARGIN_SHIFT
#define MARGIN_G1_G2_OVERWRITE_MASK   CCE_PCIE_CTRL_XMT_MARGIN_GEN1_GEN2_OVERWRITE_ENABLE_MASK
#define MARGIN_G1_G2_OVERWRITE_SHIFT  CCE_PCIE_CTRL_XMT_MARGIN_GEN1_GEN2_OVERWRITE_ENABLE_SHIFT
#define MARGIN_GEN1_GEN2_MASK         CCE_PCIE_CTRL_XMT_MARGIN_GEN1_GEN2_MASK
#define MARGIN_GEN1_GEN2_SHIFT        CCE_PCIE_CTRL_XMT_MARGIN_GEN1_GEN2_SHIFT

 /*
  * Write xmt_margin for full-swing (WFR-B) or half-swing (WFR-C).
  */
static void write_xmt_margin(struct hfi1_devdata *dd, const char *fname)
{
	u64 pcie_ctrl;
	u64 xmt_margin;
	u64 xmt_margin_oe;
	u64 lane_delay;
	u64 lane_bundle;

	pcie_ctrl = read_csr(dd, CCE_PCIE_CTRL);

	/*
	 * For Discrete, use full-swing.
	 *  - PCIe TX defaults to full-swing.
	 *    Leave this register as default.
	 * For Integrated, use half-swing
	 *  - Copy xmt_margin and xmt_margin_oe
	 *    from Gen1/Gen2 to Gen3.
	 */
	if (dd->pcidev->device == PCI_DEVICE_ID_INTEL1) { /* integrated */
		/* extract initial fields */
		xmt_margin = (pcie_ctrl >> MARGIN_GEN1_GEN2_SHIFT)
			      & MARGIN_GEN1_GEN2_MASK;
		xmt_margin_oe = (pcie_ctrl >> MARGIN_G1_G2_OVERWRITE_SHIFT)
				 & MARGIN_G1_G2_OVERWRITE_MASK;
		lane_delay = (pcie_ctrl >> LANE_DELAY_SHIFT) & LANE_DELAY_MASK;
		lane_bundle = (pcie_ctrl >> LANE_BUNDLE_SHIFT)
			       & LANE_BUNDLE_MASK;

		/*
		 * For A0, EFUSE values are not set.  Override with the
		 * correct values.
		 */
		if (is_ax(dd)) {
			/*
			 * xmt_margin and OverwiteEnabel should be the
			 * same for Gen1/Gen2 and Gen3
			 */
			xmt_margin = 0x5;
			xmt_margin_oe = 0x1;
			lane_delay = 0xF; /* Delay 240ns. */
			lane_bundle = 0x0; /* Set to 1 lane. */
		}

		/* overwrite existing values */
		pcie_ctrl = (xmt_margin << MARGIN_GEN1_GEN2_SHIFT)
			| (xmt_margin_oe << MARGIN_G1_G2_OVERWRITE_SHIFT)
			| (xmt_margin << MARGIN_SHIFT)
			| (xmt_margin_oe << MARGIN_OVERWRITE_ENABLE_SHIFT)
			| (lane_delay << LANE_DELAY_SHIFT)
			| (lane_bundle << LANE_BUNDLE_SHIFT);

		write_csr(dd, CCE_PCIE_CTRL, pcie_ctrl);
	}

	dd_dev_dbg(dd, "%s: program XMT margin, CcePcieCtrl 0x%llx\n",
		   fname, pcie_ctrl);
}

/*
 * Do all the steps needed to transition the PCIe link to Gen3 speed.
 */
int do_pcie_gen3_transition(struct hfi1_devdata *dd)
{
	struct pci_dev *parent = dd->pcidev->bus->self;
	u64 fw_ctrl;
	u64 reg, therm;
	u32 reg32, fs, lf;
	u32 status, err;
	int ret;
	int do_retry, retry_count = 0;
	int intnum = 0;
	uint default_pset;
	uint pset = pcie_pset;
	u16 target_vector, target_speed;
	u16 lnkctl2, vendor;
	u8 div;
	const u8 (*eq)[3];
	const u8 (*ctle_tunings)[4];
	uint static_ctle_mode;
	int return_error = 0;
	u32 target_width;

	/* PCIe Gen3 is for the ASIC only */
	if (dd->icode != ICODE_RTL_SILICON)
		return 0;

	if (pcie_target == 1) {			/* target Gen1 */
		target_vector = PCI_EXP_LNKCTL2_TLS_2_5GT;
		target_speed = 2500;
	} else if (pcie_target == 2) {		/* target Gen2 */
		target_vector = PCI_EXP_LNKCTL2_TLS_5_0GT;
		target_speed = 5000;
	} else if (pcie_target == 3) {		/* target Gen3 */
		target_vector = PCI_EXP_LNKCTL2_TLS_8_0GT;
		target_speed = 8000;
	} else {
		/* off or invalid target - skip */
		dd_dev_info(dd, "%s: Skipping PCIe transition\n", __func__);
		return 0;
	}

	/* if already at target speed, done (unless forced) */
	if (dd->lbus_speed == target_speed) {
		dd_dev_info(dd, "%s: PCIe already at gen%d, %s\n", __func__,
			    pcie_target,
			    pcie_force ? "re-doing anyway" : "skipping");
		if (!pcie_force)
			return 0;
	}

	/*
	 * The driver cannot do the transition if it has no access to the
	 * upstream component
	 */
	if (!parent) {
		dd_dev_info(dd, "%s: No upstream, Can't do gen3 transition\n",
			    __func__);
		return 0;
	}

	/* Previous Gen1/Gen2 bus width */
	target_width = dd->lbus_width;

	/*
	 * Do the Gen3 transition.  Steps are those of the PCIe Gen3
	 * recipe.
	 */

	/* step 1: pcie link working in gen1/gen2 */

	/* step 2: if either side is not capable of Gen3, done */
	if (pcie_target == 3 && !dd->link_gen3_capable) {
		dd_dev_err(dd, "The PCIe link is not Gen3 capable\n");
		ret = -ENOSYS;
		goto done_no_mutex;
	}

	/* hold the SBus resource across the firmware download and SBR */
	ret = acquire_chip_resource(dd, CR_SBUS, SBUS_TIMEOUT);
	if (ret) {
		dd_dev_err(dd, "%s: unable to acquire SBus resource\n",
			   __func__);
		return ret;
	}

	/* make sure thermal polling is not causing interrupts */
	therm = read_csr(dd, ASIC_CFG_THERM_POLL_EN);
	if (therm) {
		write_csr(dd, ASIC_CFG_THERM_POLL_EN, 0x0);
		msleep(100);
		dd_dev_info(dd, "%s: Disabled therm polling\n",
			    __func__);
	}

retry:
	/* the SBus download will reset the spico for thermal */

	/* step 3: download SBus Master firmware */
	/* step 4: download PCIe Gen3 SerDes firmware */
	dd_dev_info(dd, "%s: downloading firmware\n", __func__);
	ret = load_pcie_firmware(dd);
	if (ret) {
		/* do not proceed if the firmware cannot be downloaded */
		return_error = 1;
		goto done;
	}

	/* step 5: set up device parameter settings */
	dd_dev_info(dd, "%s: setting PCIe registers\n", __func__);

	/*
	 * PcieCfgSpcie1 - Link Control 3
	 * Leave at reset value.  No need to set PerfEq - link equalization
	 * will be performed automatically after the SBR when the target
	 * speed is 8GT/s.
	 */

	/* clear all 16 per-lane error bits (PCIe: Lane Error Status) */
	pci_write_config_dword(dd->pcidev, PCIE_CFG_SPCIE2, 0xffff);

	/* step 5a: Set Synopsys Port Logic registers */

	/*
	 * PcieCfgRegPl2 - Port Force Link
	 *
	 * Set the low power field to 0x10 to avoid unnecessary power
	 * management messages.  All other fields are zero.
	 */
	reg32 = 0x10ul << PCIE_CFG_REG_PL2_LOW_PWR_ENT_CNT_SHIFT;
	pci_write_config_dword(dd->pcidev, PCIE_CFG_REG_PL2, reg32);

	/*
	 * PcieCfgRegPl100 - Gen3 Control
	 *
	 * turn off PcieCfgRegPl100.Gen3ZRxDcNonCompl
	 * turn on PcieCfgRegPl100.EqEieosCnt
	 * Everything else zero.
	 */
	reg32 = PCIE_CFG_REG_PL100_EQ_EIEOS_CNT_SMASK;
	pci_write_config_dword(dd->pcidev, PCIE_CFG_REG_PL100, reg32);

	/*
	 * PcieCfgRegPl101 - Gen3 EQ FS and LF
	 * PcieCfgRegPl102 - Gen3 EQ Presets to Coefficients Mapping
	 * PcieCfgRegPl103 - Gen3 EQ Preset Index
	 * PcieCfgRegPl105 - Gen3 EQ Status
	 *
	 * Give initial EQ settings.
	 */
	if (dd->pcidev->device == PCI_DEVICE_ID_INTEL0) { /* discrete */
		/* 1000mV, FS=24, LF = 8 */
		fs = 24;
		lf = 8;
		div = 3;
		eq = discrete_preliminary_eq;
		default_pset = DEFAULT_DISCRETE_PSET;
		ctle_tunings = discrete_ctle_tunings;
		/* bit 0 - discrete on/off */
		static_ctle_mode = pcie_ctle & 0x1;
	} else {
		/* 400mV, FS=29, LF = 9 */
		fs = 29;
		lf = 9;
		div = 1;
		eq = integrated_preliminary_eq;
		default_pset = DEFAULT_MCP_PSET;
		ctle_tunings = integrated_ctle_tunings;
		/* bit 1 - integrated on/off */
		static_ctle_mode = (pcie_ctle >> 1) & 0x1;
	}
	pci_write_config_dword(dd->pcidev, PCIE_CFG_REG_PL101,
			       (fs <<
				PCIE_CFG_REG_PL101_GEN3_EQ_LOCAL_FS_SHIFT) |
			       (lf <<
				PCIE_CFG_REG_PL101_GEN3_EQ_LOCAL_LF_SHIFT));
	ret = load_eq_table(dd, eq, fs, div);
	if (ret)
		goto done;

	/*
	 * PcieCfgRegPl106 - Gen3 EQ Control
	 *
	 * Set Gen3EqPsetReqVec, leave other fields 0.
	 */
	if (pset == UNSET_PSET)
		pset = default_pset;
	if (pset > 10) {	/* valid range is 0-10, inclusive */
		dd_dev_err(dd, "%s: Invalid Eq Pset %u, setting to %d\n",
			   __func__, pset, default_pset);
		pset = default_pset;
	}
	dd_dev_info(dd, "%s: using EQ Pset %u\n", __func__, pset);
	pci_write_config_dword(dd->pcidev, PCIE_CFG_REG_PL106,
			       ((1 << pset) <<
			PCIE_CFG_REG_PL106_GEN3_EQ_PSET_REQ_VEC_SHIFT) |
			PCIE_CFG_REG_PL106_GEN3_EQ_EVAL2MS_DISABLE_SMASK |
			PCIE_CFG_REG_PL106_GEN3_EQ_PHASE23_EXIT_MODE_SMASK);

	/*
	 * step 5b: Do post firmware download steps via SBus
	 */
	dd_dev_info(dd, "%s: doing pcie post steps\n", __func__);
	pcie_post_steps(dd);

	/*
	 * step 5c: Program gasket interrupts
	 */
	/* set the Rx Bit Rate to REFCLK ratio */
	write_gasket_interrupt(dd, intnum++, 0x0006, 0x0050);
	/* disable pCal for PCIe Gen3 RX equalization */
	/* select adaptive or static CTLE */
	write_gasket_interrupt(dd, intnum++, 0x0026,
			       0x5b01 | (static_ctle_mode << 3));
	/*
	 * Enable iCal for PCIe Gen3 RX equalization, and set which
	 * evaluation of RX_EQ_EVAL will launch the iCal procedure.
	 */
	write_gasket_interrupt(dd, intnum++, 0x0026, 0x5202);

	if (static_ctle_mode) {
		/* apply static CTLE tunings */
		u8 pcie_dc, pcie_lf, pcie_hf, pcie_bw;

		pcie_dc = ctle_tunings[pset][0];
		pcie_lf = ctle_tunings[pset][1];
		pcie_hf = ctle_tunings[pset][2];
		pcie_bw = ctle_tunings[pset][3];
		write_gasket_interrupt(dd, intnum++, 0x0026, 0x0200 | pcie_dc);
		write_gasket_interrupt(dd, intnum++, 0x0026, 0x0100 | pcie_lf);
		write_gasket_interrupt(dd, intnum++, 0x0026, 0x0000 | pcie_hf);
		write_gasket_interrupt(dd, intnum++, 0x0026, 0x5500 | pcie_bw);
	}

	/* terminate list */
	write_gasket_interrupt(dd, intnum++, 0x0000, 0x0000);

	/*
	 * step 5d: program XMT margin
	 */
	write_xmt_margin(dd, __func__);

	/*
	 * step 5e: disable active state power management (ASPM). It
	 * will be enabled if required later
	 */
	dd_dev_info(dd, "%s: clearing ASPM\n", __func__);
	aspm_hw_disable_l1(dd);

	/*
	 * step 5f: clear DirectSpeedChange
	 * PcieCfgRegPl67.DirectSpeedChange must be zero to prevent the
	 * change in the speed target from starting before we are ready.
	 * This field defaults to 0 and we are not changing it, so nothing
	 * needs to be done.
	 */

	/* step 5g: Set target link speed */
	/*
	 * Set target link speed to be target on both device and parent.
	 * On setting the parent: Some system BIOSs "helpfully" set the
	 * parent target speed to Gen2 to match the ASIC's initial speed.
	 * We can set the target Gen3 because we have already checked
	 * that it is Gen3 capable earlier.
	 */
	dd_dev_info(dd, "%s: setting parent target link speed\n", __func__);
	ret = pcie_capability_read_word(parent, PCI_EXP_LNKCTL2, &lnkctl2);
	if (ret) {
		dd_dev_err(dd, "Unable to read from PCI config\n");
		return_error = 1;
		goto done;
	}

	dd_dev_info(dd, "%s: ..old link control2: 0x%x\n", __func__,
		    (u32)lnkctl2);
	/* only write to parent if target is not as high as ours */
	if ((lnkctl2 & PCI_EXP_LNKCTL2_TLS) < target_vector) {
		lnkctl2 &= ~PCI_EXP_LNKCTL2_TLS;
		lnkctl2 |= target_vector;
		dd_dev_info(dd, "%s: ..new link control2: 0x%x\n", __func__,
			    (u32)lnkctl2);
		ret = pcie_capability_write_word(parent,
						 PCI_EXP_LNKCTL2, lnkctl2);
		if (ret) {
			dd_dev_err(dd, "Unable to write to PCI config\n");
			return_error = 1;
			goto done;
		}
	} else {
		dd_dev_info(dd, "%s: ..target speed is OK\n", __func__);
	}

	dd_dev_info(dd, "%s: setting target link speed\n", __func__);
	ret = pcie_capability_read_word(dd->pcidev, PCI_EXP_LNKCTL2, &lnkctl2);
	if (ret) {
		dd_dev_err(dd, "Unable to read from PCI config\n");
		return_error = 1;
		goto done;
	}

	dd_dev_info(dd, "%s: ..old link control2: 0x%x\n", __func__,
		    (u32)lnkctl2);
	lnkctl2 &= ~PCI_EXP_LNKCTL2_TLS;
	lnkctl2 |= target_vector;
	dd_dev_info(dd, "%s: ..new link control2: 0x%x\n", __func__,
		    (u32)lnkctl2);
	ret = pcie_capability_write_word(dd->pcidev, PCI_EXP_LNKCTL2, lnkctl2);
	if (ret) {
		dd_dev_err(dd, "Unable to write to PCI config\n");
		return_error = 1;
		goto done;
	}

	/* step 5h: arm gasket logic */
	/* hold DC in reset across the SBR */
	write_csr(dd, CCE_DC_CTRL, CCE_DC_CTRL_DC_RESET_SMASK);
	(void)read_csr(dd, CCE_DC_CTRL); /* DC reset hold */
	/* save firmware control across the SBR */
	fw_ctrl = read_csr(dd, MISC_CFG_FW_CTRL);

	dd_dev_info(dd, "%s: arming gasket logic\n", __func__);
	arm_gasket_logic(dd);

	/*
	 * step 6: quiesce PCIe link
	 * The chip has already been reset, so there will be no traffic
	 * from the chip.  Linux has no easy way to enforce that it will
	 * not try to access the device, so we just need to hope it doesn't
	 * do it while we are doing the reset.
	 */

	/*
	 * step 7: initiate the secondary bus reset (SBR)
	 * step 8: hardware brings the links back up
	 * step 9: wait for link speed transition to be complete
	 */
	dd_dev_info(dd, "%s: calling trigger_sbr\n", __func__);
	ret = trigger_sbr(dd);
	if (ret)
		goto done;

	/* step 10: decide what to do next */

	/* check if we can read PCI space */
	ret = pci_read_config_word(dd->pcidev, PCI_VENDOR_ID, &vendor);
	if (ret) {
		dd_dev_info(dd,
			    "%s: read of VendorID failed after SBR, err %d\n",
			    __func__, ret);
		return_error = 1;
		goto done;
	}
	if (vendor == 0xffff) {
		dd_dev_info(dd, "%s: VendorID is all 1s after SBR\n", __func__);
		return_error = 1;
		ret = -EIO;
		goto done;
	}

	/* restore PCI space registers we know were reset */
	dd_dev_info(dd, "%s: calling restore_pci_variables\n", __func__);
	ret = restore_pci_variables(dd);
	if (ret) {
		dd_dev_err(dd, "%s: Could not restore PCI variables\n",
			   __func__);
		return_error = 1;
		goto done;
	}

	/* restore firmware control */
	write_csr(dd, MISC_CFG_FW_CTRL, fw_ctrl);

	/*
	 * Check the gasket block status.
	 *
	 * This is the first CSR read after the SBR.  If the read returns
	 * all 1s (fails), the link did not make it back.
	 *
	 * Once we're sure we can read and write, clear the DC reset after
	 * the SBR.  Then check for any per-lane errors. Then look over
	 * the status.
	 */
	reg = read_csr(dd, ASIC_PCIE_SD_HOST_STATUS);
	dd_dev_info(dd, "%s: gasket block status: 0x%llx\n", __func__, reg);
	if (reg == ~0ull) {	/* PCIe read failed/timeout */
		dd_dev_err(dd, "SBR failed - unable to read from device\n");
		return_error = 1;
		ret = -ENOSYS;
		goto done;
	}

	/* clear the DC reset */
	write_csr(dd, CCE_DC_CTRL, 0);

	/* Set the LED off */
	setextled(dd, 0);

	/* check for any per-lane errors */
	ret = pci_read_config_dword(dd->pcidev, PCIE_CFG_SPCIE2, &reg32);
	if (ret) {
		dd_dev_err(dd, "Unable to read from PCI config\n");
		return_error = 1;
		goto done;
	}

	dd_dev_info(dd, "%s: per-lane errors: 0x%x\n", __func__, reg32);

	/* extract status, look for our HFI */
	status = (reg >> ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_STS_SHIFT)
			& ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_STS_MASK;
	if ((status & (1 << dd->hfi1_id)) == 0) {
		dd_dev_err(dd,
			   "%s: gasket status 0x%x, expecting 0x%x\n",
			   __func__, status, 1 << dd->hfi1_id);
		ret = -EIO;
		goto done;
	}

	/* extract error */
	err = (reg >> ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_ERR_SHIFT)
		& ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_ERR_MASK;
	if (err) {
		dd_dev_err(dd, "%s: gasket error %d\n", __func__, err);
		ret = -EIO;
		goto done;
	}

	/* update our link information cache */
	update_lbus_info(dd);
	dd_dev_info(dd, "%s: new speed and width: %s\n", __func__,
		    dd->lbus_info);

	if (dd->lbus_speed != target_speed ||
	    dd->lbus_width < target_width) { /* not target */
		/* maybe retry */
		do_retry = retry_count < pcie_retry;
		dd_dev_err(dd, "PCIe link speed or width did not match target%s\n",
			   do_retry ? ", retrying" : "");
		retry_count++;
		if (do_retry) {
			msleep(100); /* allow time to settle */
			goto retry;
		}
		ret = -EIO;
	}

done:
	if (therm) {
		write_csr(dd, ASIC_CFG_THERM_POLL_EN, 0x1);
		msleep(100);
		dd_dev_info(dd, "%s: Re-enable therm polling\n",
			    __func__);
	}
	release_chip_resource(dd, CR_SBUS);
done_no_mutex:
	/* return no error if it is OK to be at current speed */
	if (ret && !return_error) {
		dd_dev_err(dd, "Proceeding at current speed PCIe speed\n");
		ret = 0;
	}

	dd_dev_info(dd, "%s: done\n", __func__);
	return ret;
}
