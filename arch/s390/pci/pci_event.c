/*
 *  Copyright IBM Corp. 2012
 *
 *  Author(s):
 *    Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/pci_debug.h>
#include <asm/sclp.h>

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

void zpci_event_error(void *data)
{
	struct zpci_ccdf_err *ccdf = data;
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);

	zpci_err("error CCDF:\n");
	zpci_err_hex(ccdf, sizeof(*ccdf));

	if (!zdev)
		return;

	pr_err("%s: Event 0x%x reports an error for PCI function 0x%x\n",
	       pci_name(zdev->pdev), ccdf->pec, ccdf->fid);
}

void zpci_event_availability(void *data)
{
	struct zpci_ccdf_avail *ccdf = data;
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);
	struct pci_dev *pdev = zdev ? zdev->pdev : NULL;
	int ret;

	pr_info("%s: Event 0x%x reconfigured PCI function 0x%x\n",
		pdev ? pci_name(pdev) : "n/a", ccdf->pec, ccdf->fid);
	zpci_err("avail CCDF:\n");
	zpci_err_hex(ccdf, sizeof(*ccdf));

	switch (ccdf->pec) {
	case 0x0301: /* Standby -> Configured */
		if (!zdev || zdev->state == ZPCI_FN_STATE_CONFIGURED)
			break;
		zdev->state = ZPCI_FN_STATE_CONFIGURED;
		ret = zpci_enable_device(zdev);
		if (ret)
			break;
		pci_rescan_bus(zdev->bus);
		break;
	case 0x0302: /* Reserved -> Standby */
		clp_add_pci_device(ccdf->fid, ccdf->fh, 0);
		break;
	case 0x0303: /* Deconfiguration requested */
		if (pdev)
			pci_stop_and_remove_bus_device(pdev);

		ret = zpci_disable_device(zdev);
		if (ret)
			break;

		ret = sclp_pci_deconfigure(zdev->fid);
		zpci_dbg(3, "deconf fid:%x, rc:%d\n", zdev->fid, ret);
		if (!ret)
			zdev->state = ZPCI_FN_STATE_STANDBY;

		break;
	case 0x0304: /* Configured -> Standby */
		if (pdev)
			pci_stop_and_remove_bus_device(pdev);

		zpci_disable_device(zdev);
		zdev->state = ZPCI_FN_STATE_STANDBY;
		break;
	case 0x0306: /* 0x308 or 0x302 for multiple devices */
		clp_rescan_pci_devices();
		break;
	case 0x0308: /* Standby -> Reserved */
		pci_stop_root_bus(zdev->bus);
		pci_remove_root_bus(zdev->bus);
		break;
	default:
		break;
	}
}
