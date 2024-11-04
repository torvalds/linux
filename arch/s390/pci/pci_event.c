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

static inline bool ers_result_indicates_abort(pci_ers_result_t ers_res)
{
	switch (ers_res) {
	case PCI_ERS_RESULT_CAN_RECOVER:
	case PCI_ERS_RESULT_RECOVERED:
	case PCI_ERS_RESULT_NEED_RESET:
		return false;
	default:
		return true;
	}
}

static bool is_passed_through(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	bool ret;

	mutex_lock(&zdev->kzdev_lock);
	ret = !!zdev->kzdev;
	mutex_unlock(&zdev->kzdev_lock);

	return ret;
}

static bool is_driver_supported(struct pci_driver *driver)
{
	if (!driver || !driver->err_handler)
		return false;
	if (!driver->err_handler->error_detected)
		return false;
	if (!driver->err_handler->slot_reset)
		return false;
	if (!driver->err_handler->resume)
		return false;
	return true;
}

static pci_ers_result_t zpci_event_notify_error_detected(struct pci_dev *pdev,
							 struct pci_driver *driver)
{
	pci_ers_result_t ers_res = PCI_ERS_RESULT_DISCONNECT;

	ers_res = driver->err_handler->error_detected(pdev,  pdev->error_state);
	if (ers_result_indicates_abort(ers_res))
		pr_info("%s: Automatic recovery failed after initial reporting\n", pci_name(pdev));
	else if (ers_res == PCI_ERS_RESULT_NEED_RESET)
		pr_debug("%s: Driver needs reset to recover\n", pci_name(pdev));

	return ers_res;
}

static pci_ers_result_t zpci_event_do_error_state_clear(struct pci_dev *pdev,
							struct pci_driver *driver)
{
	pci_ers_result_t ers_res = PCI_ERS_RESULT_DISCONNECT;
	struct zpci_dev *zdev = to_zpci(pdev);
	int rc;

	pr_info("%s: Unblocking device access for examination\n", pci_name(pdev));
	rc = zpci_reset_load_store_blocked(zdev);
	if (rc) {
		pr_err("%s: Unblocking device access failed\n", pci_name(pdev));
		/* Let's try a full reset instead */
		return PCI_ERS_RESULT_NEED_RESET;
	}

	if (driver->err_handler->mmio_enabled) {
		ers_res = driver->err_handler->mmio_enabled(pdev);
		if (ers_result_indicates_abort(ers_res)) {
			pr_info("%s: Automatic recovery failed after MMIO re-enable\n",
				pci_name(pdev));
			return ers_res;
		} else if (ers_res == PCI_ERS_RESULT_NEED_RESET) {
			pr_debug("%s: Driver needs reset to recover\n", pci_name(pdev));
			return ers_res;
		}
	}

	pr_debug("%s: Unblocking DMA\n", pci_name(pdev));
	rc = zpci_clear_error_state(zdev);
	if (!rc) {
		pdev->error_state = pci_channel_io_normal;
	} else {
		pr_err("%s: Unblocking DMA failed\n", pci_name(pdev));
		/* Let's try a full reset instead */
		return PCI_ERS_RESULT_NEED_RESET;
	}

	return ers_res;
}

static pci_ers_result_t zpci_event_do_reset(struct pci_dev *pdev,
					    struct pci_driver *driver)
{
	pci_ers_result_t ers_res = PCI_ERS_RESULT_DISCONNECT;

	pr_info("%s: Initiating reset\n", pci_name(pdev));
	if (zpci_hot_reset_device(to_zpci(pdev))) {
		pr_err("%s: The reset request failed\n", pci_name(pdev));
		return ers_res;
	}
	pdev->error_state = pci_channel_io_normal;
	ers_res = driver->err_handler->slot_reset(pdev);
	if (ers_result_indicates_abort(ers_res)) {
		pr_info("%s: Automatic recovery failed after slot reset\n", pci_name(pdev));
		return ers_res;
	}

	return ers_res;
}

/* zpci_event_attempt_error_recovery - Try to recover the given PCI function
 * @pdev: PCI function to recover currently in the error state
 *
 * We follow the scheme outlined in Documentation/PCI/pci-error-recovery.rst.
 * With the simplification that recovery always happens per function
 * and the platform determines which functions are affected for
 * multi-function devices.
 */
static pci_ers_result_t zpci_event_attempt_error_recovery(struct pci_dev *pdev)
{
	pci_ers_result_t ers_res = PCI_ERS_RESULT_DISCONNECT;
	struct pci_driver *driver;

	/*
	 * Ensure that the PCI function is not removed concurrently, no driver
	 * is unbound or probed and that userspace can't access its
	 * configuration space while we perform recovery.
	 */
	pci_dev_lock(pdev);
	if (pdev->error_state == pci_channel_io_perm_failure) {
		ers_res = PCI_ERS_RESULT_DISCONNECT;
		goto out_unlock;
	}
	pdev->error_state = pci_channel_io_frozen;

	if (is_passed_through(pdev)) {
		pr_info("%s: Cannot be recovered in the host because it is a pass-through device\n",
			pci_name(pdev));
		goto out_unlock;
	}

	driver = to_pci_driver(pdev->dev.driver);
	if (!is_driver_supported(driver)) {
		if (!driver)
			pr_info("%s: Cannot be recovered because no driver is bound to the device\n",
				pci_name(pdev));
		else
			pr_info("%s: The %s driver bound to the device does not support error recovery\n",
				pci_name(pdev),
				driver->name);
		goto out_unlock;
	}

	ers_res = zpci_event_notify_error_detected(pdev, driver);
	if (ers_result_indicates_abort(ers_res))
		goto out_unlock;

	if (ers_res == PCI_ERS_RESULT_CAN_RECOVER) {
		ers_res = zpci_event_do_error_state_clear(pdev, driver);
		if (ers_result_indicates_abort(ers_res))
			goto out_unlock;
	}

	if (ers_res == PCI_ERS_RESULT_NEED_RESET)
		ers_res = zpci_event_do_reset(pdev, driver);

	if (ers_res != PCI_ERS_RESULT_RECOVERED) {
		pr_err("%s: Automatic recovery failed; operator intervention is required\n",
		       pci_name(pdev));
		goto out_unlock;
	}

	pr_info("%s: The device is ready to resume operations\n", pci_name(pdev));
	if (driver->err_handler->resume)
		driver->err_handler->resume(pdev);
out_unlock:
	pci_dev_unlock(pdev);

	return ers_res;
}

/* zpci_event_io_failure - Report PCI channel failure state to driver
 * @pdev: PCI function for which to report
 * @es: PCI channel failure state to report
 */
static void zpci_event_io_failure(struct pci_dev *pdev, pci_channel_state_t es)
{
	struct pci_driver *driver;

	pci_dev_lock(pdev);
	pdev->error_state = es;
	/**
	 * While vfio-pci's error_detected callback notifies user-space QEMU
	 * reacts to this by freezing the guest. In an s390 environment PCI
	 * errors are rarely fatal so this is overkill. Instead in the future
	 * we will inject the error event and let the guest recover the device
	 * itself.
	 */
	if (is_passed_through(pdev))
		goto out;
	driver = to_pci_driver(pdev->dev.driver);
	if (driver && driver->err_handler && driver->err_handler->error_detected)
		driver->err_handler->error_detected(pdev, pdev->error_state);
out:
	pci_dev_unlock(pdev);
}

static void __zpci_event_error(struct zpci_ccdf_err *ccdf)
{
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);
	struct pci_dev *pdev = NULL;
	pci_ers_result_t ers_res;

	zpci_dbg(3, "err fid:%x, fh:%x, pec:%x\n",
		 ccdf->fid, ccdf->fh, ccdf->pec);
	zpci_err("error CCDF:\n");
	zpci_err_hex(ccdf, sizeof(*ccdf));

	if (zdev) {
		mutex_lock(&zdev->state_lock);
		zpci_update_fh(zdev, ccdf->fh);
		if (zdev->zbus->bus)
			pdev = pci_get_slot(zdev->zbus->bus, zdev->devfn);
	}

	pr_err("%s: Event 0x%x reports an error for PCI function 0x%x\n",
	       pdev ? pci_name(pdev) : "n/a", ccdf->pec, ccdf->fid);

	if (!pdev)
		goto no_pdev;

	switch (ccdf->pec) {
	case 0x002a: /* Error event concerns FMB */
	case 0x002b:
	case 0x002c:
		break;
	case 0x0040: /* Service Action or Error Recovery Failed */
	case 0x003b:
		zpci_event_io_failure(pdev, pci_channel_io_perm_failure);
		break;
	default: /* PCI function left in the error state attempt to recover */
		ers_res = zpci_event_attempt_error_recovery(pdev);
		if (ers_res != PCI_ERS_RESULT_RECOVERED)
			zpci_event_io_failure(pdev, pci_channel_io_perm_failure);
		break;
	}
	pci_dev_put(pdev);
no_pdev:
	if (zdev)
		mutex_unlock(&zdev->state_lock);
	zpci_zdev_put(zdev);
}

void zpci_event_error(void *data)
{
	if (zpci_is_enabled())
		__zpci_event_error(data);
}

static void zpci_event_hard_deconfigured(struct zpci_dev *zdev, u32 fh)
{
	zpci_update_fh(zdev, fh);
	/* Give the driver a hint that the function is
	 * already unusable.
	 */
	zpci_bus_remove_device(zdev, true);
	/* Even though the device is already gone we still
	 * need to free zPCI resources as part of the disable.
	 */
	if (zdev_enabled(zdev))
		zpci_disable_device(zdev);
	zdev->state = ZPCI_FN_STATE_STANDBY;
}

static void __zpci_event_availability(struct zpci_ccdf_avail *ccdf)
{
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);
	bool existing_zdev = !!zdev;
	enum zpci_state state;

	zpci_dbg(3, "avl fid:%x, fh:%x, pec:%x\n",
		 ccdf->fid, ccdf->fh, ccdf->pec);

	if (existing_zdev)
		mutex_lock(&zdev->state_lock);

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
		zpci_scan_configured_device(zdev, ccdf->fh);
		break;
	case 0x0302: /* Reserved -> Standby */
		if (!zdev)
			zpci_create_device(ccdf->fid, ccdf->fh, ZPCI_FN_STATE_STANDBY);
		else
			zpci_update_fh(zdev, ccdf->fh);
		break;
	case 0x0303: /* Deconfiguration requested */
		if (zdev) {
			/* The event may have been queued before we configured
			 * the device.
			 */
			if (zdev->state != ZPCI_FN_STATE_CONFIGURED)
				break;
			zpci_update_fh(zdev, ccdf->fh);
			zpci_deconfigure_device(zdev);
		}
		break;
	case 0x0304: /* Configured -> Standby|Reserved */
		if (zdev) {
			/* The event may have been queued before we configured
			 * the device.:
			 */
			if (zdev->state == ZPCI_FN_STATE_CONFIGURED)
				zpci_event_hard_deconfigured(zdev, ccdf->fh);
			/* The 0x0304 event may immediately reserve the device */
			if (!clp_get_state(zdev->fid, &state) &&
			    state == ZPCI_FN_STATE_RESERVED) {
				zpci_device_reserved(zdev);
			}
		}
		break;
	case 0x0306: /* 0x308 or 0x302 for multiple devices */
		zpci_remove_reserved_devices();
		clp_scan_pci_devices();
		break;
	case 0x0308: /* Standby -> Reserved */
		if (!zdev)
			break;
		zpci_device_reserved(zdev);
		break;
	default:
		break;
	}
	if (existing_zdev) {
		mutex_unlock(&zdev->state_lock);
		zpci_zdev_put(zdev);
	}
}

void zpci_event_availability(void *data)
{
	if (zpci_is_enabled())
		__zpci_event_availability(data);
}
