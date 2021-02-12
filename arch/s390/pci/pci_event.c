// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright IBM Corp. 2012
 *
 *  Author(s):
 *    Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/pci_debug.h>
#include <asm/pci_dma.h>
#include <asm/sclp.h>

#include "pci_bus.h"

/* Content Code Description for PCI Function Error */
struct zpci_ccdf_err {
	u32 reserved1;
	u32 fh;				/* function handle */
	u32 fid;			/* function id */
	u32 ett		:  4;		/* expected table type */
	u32 mvn		: 12;		/* MSI vector number */
	u32 dmaas	:  8;		/* DMA address space */
	u32		:  6;
	u32 q		:  1;		/* event qualifier */
	u32 rw		:  1;		/* read/write */
	u64 faddr;			/* failing address */
	u32 reserved3;
	u16 reserved4;
	u16 pec;			/* PCI event code */
} __packed;

/* Content Code Description for PCI Function Availability */
struct zpci_ccdf_avail {
	u32 reserved1;
	u32 fh;				/* function handle */
	u32 fid;			/* function id */
	u32 reserved2;
	u32 reserved3;
	u32 reserved4;
	u32 reserved5;
	u16 reserved6;
	u16 pec;			/* PCI event code */
} __packed;

static void __zpci_event_error(struct zpci_ccdf_err *ccdf)
{
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);
	struct pci_dev *pdev = NULL;

	zpci_err("error CCDF:\n");
	zpci_err_hex(ccdf, sizeof(*ccdf));

	if (zdev)
		pdev = pci_get_slot(zdev->zbus->bus, zdev->devfn);

	pr_err("%s: Event 0x%x reports an error for PCI function 0x%x\n",
	       pdev ? pci_name(pdev) : "n/a", ccdf->pec, ccdf->fid);

	if (!pdev)
		return;

	pdev->error_state = pci_channel_io_perm_failure;
	pci_dev_put(pdev);
}

void zpci_event_error(void *data)
{
	if (zpci_is_enabled())
		__zpci_event_error(data);
}

static void zpci_event_hard_deconfigured(struct zpci_dev *zdev, u32 fh)
{
	enum zpci_state state;

	zdev->fh = fh;
	/* Give the driver a hint that the function is
	 * already unusable.
	 */
	zpci_bus_remove_device(zdev, true);
	/* Even though the device is already gone we still
	 * need to free zPCI resources as part of the disable.
	 */
	zpci_disable_device(zdev);
	zdev->state = ZPCI_FN_STATE_STANDBY;
	if (!clp_get_state(zdev->fid, &state) &&
	    state == ZPCI_FN_STATE_RESERVED) {
		zpci_zdev_put(zdev);
	}
}

static void __zpci_event_availability(struct zpci_ccdf_avail *ccdf)
{
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);

	zpci_err("avail CCDF:\n");
	zpci_err_hex(ccdf, sizeof(*ccdf));

	switch (ccdf->pec) {
	case 0x0301: /* Reserved|Standby -> Configured */
		if (!zdev) {
			zdev = zpci_create_device(ccdf->fid, ccdf->fh, ZPCI_FN_STATE_CONFIGURED);
			if (IS_ERR(zdev))
				break;
		} else {
			/* the configuration request may be stale */
			if (zdev->state != ZPCI_FN_STATE_STANDBY)
				break;
			zdev->state = ZPCI_FN_STATE_CONFIGURED;
		}
		zpci_configure_device(zdev, ccdf->fh);
		break;
	case 0x0302: /* Reserved -> Standby */
		if (!zdev)
			zpci_create_device(ccdf->fid, ccdf->fh, ZPCI_FN_STATE_STANDBY);
		else
			zdev->fh = ccdf->fh;
		break;
	case 0x0303: /* Deconfiguration requested */
		if (zdev) {
			zdev->fh = ccdf->fh;
			zpci_deconfigure_device(zdev);
		}
		break;
	case 0x0304: /* Configured -> Standby|Reserved */
		if (zdev)
			zpci_event_hard_deconfigured(zdev, ccdf->fh);
		break;
	case 0x0306: /* 0x308 or 0x302 for multiple devices */
		zpci_remove_reserved_devices();
		clp_scan_pci_devices();
		break;
	case 0x0308: /* Standby -> Reserved */
		if (!zdev)
			break;
		zpci_zdev_put(zdev);
		break;
	default:
		break;
	}
}

void zpci_event_availability(void *data)
{
	if (zpci_is_enabled())
		__zpci_event_availability(data);
}
