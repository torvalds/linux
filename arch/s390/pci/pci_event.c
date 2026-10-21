// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright IBM Corp. 2012
 *
 *  Author(s):
 *    Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define pr_fmt(fmt) "zpci: " fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/pci_debug.h>
#include <asm/pci_dma.h>
#include <asm/sclp.h>

#include "pci_bus.h"
#include "pci_report.h"

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
	case PCI_ERS_RESULT_NONE:
		return false;
	default:
		return true;
	}
}

static bool is_driver_supported(struct pci_driver *driver)
{
	if (!driver || !driver->err_handler)
		return false;
	if (!driver->err_handler->error_detected)
		return false;
	return true;
}

static int zpci_store_pci_error(struct pci_dev *pdev,
				 struct zpci_ccdf_err *ccdf)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	int i;

	guard(mutex)(&zdev->pending_errs_lock);
	if (!zdev->pending_errs.mediated_recovery)
		return -EINVAL;

	if (zdev->pending_errs.count >= ZPCI_ERR_PENDING_MAX) {
		dev_warn_ratelimited(&pdev->dev,
				     "%s: Maximum number (%d) of pending error events queued\n",
				     pci_name(pdev),
				     ZPCI_ERR_PENDING_MAX);
		return -ENOMEM;
	}

	i = zdev->pending_errs.tail % ZPCI_ERR_PENDING_MAX;
	memcpy(&zdev->pending_errs.err[i], ccdf, sizeof(struct zpci_ccdf_err));
	zdev->pending_errs.tail++;
	zdev->pending_errs.count++;
	return 0;
}

int zpci_get_pending_error(struct zpci_dev *zdev,
			   struct zpci_ccdf_err *ccdf)
{
	int head;

	guard(mutex)(&zdev->pending_errs_lock);

	if (!zdev->pending_errs.count)
		return -ENOMSG;

	head = zdev->pending_errs.head % ZPCI_ERR_PENDING_MAX;
	memcpy(ccdf, &zdev->pending_errs.err[head],
	       sizeof(struct zpci_ccdf_err));
	zdev->pending_errs.head++;
	zdev->pending_errs.count--;
	return 0;
}
EXPORT_SYMBOL_GPL(zpci_get_pending_error);

void zpci_start_mediated_recovery(struct zpci_dev *zdev)
{
	guard(mutex)(&zdev->pending_errs_lock);
	zdev->pending_errs.mediated_recovery = true;
}
EXPORT_SYMBOL_GPL(zpci_start_mediated_recovery);

void zpci_stop_mediated_recovery(struct zpci_dev *zdev)
{
	guard(mutex)(&zdev->pending_errs_lock);
	zdev->pending_errs.mediated_recovery = false;
	if (zdev->pending_errs.count)
		pr_info("Unhandled PCI error events count=%d for PCI function 0x%x\n",
			zdev->pending_errs.count, zdev->fid);
	memset(&zdev->pending_errs, 0, sizeof(struct zpci_ccdf_pending));
}
EXPORT_SYMBOL_GPL(zpci_stop_mediated_recovery);

static pci_ers_result_t zpci_event_notify_error_detected(struct pci_dev *pdev,
							 struct pci_driver *driver)
{
	pci_ers_result_t ers_res = PCI_ERS_RESULT_DISCONNECT;

	ers_res = driver->err_handler->error_detected(pdev,  pdev->error_state);
	pci_uevent_ers(pdev, ers_res);
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

	/* The underlying device may have been disabled by the event */
	if (!zdev_enabled(zdev))
		return PCI_ERS_RESULT_NEED_RESET;

	pr_info("%s: Unblocking device access for examination\n", pci_name(pdev));
	rc = zpci_reset_load_store_blocked(zdev);
	if (rc) {
		pr_err("%s: Unblocking device access failed\n", pci_name(pdev));
		/* Let's try a full reset instead */
		return PCI_ERS_RESULT_NEED_RESET;
	}

	if (driver->err_handler->mmio_enabled)
		ers_res = driver->err_handler->mmio_enabled(pdev);
	else
		ers_res = PCI_ERS_RESULT_NONE;

	if (ers_result_indicates_abort(ers_res)) {
		pr_info("%s: Automatic recovery failed after MMIO re-enable\n",
			pci_name(pdev));
		return ers_res;
	} else if (ers_res == PCI_ERS_RESULT_NEED_RESET) {
		pr_debug("%s: Driver needs reset to recover\n", pci_name(pdev));
		return ers_res;
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

	if (driver->err_handler->slot_reset)
		ers_res = driver->err_handler->slot_reset(pdev);
	else
		ers_res = PCI_ERS_RESULT_NONE;

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
static pci_ers_result_t zpci_event_attempt_error_recovery(struct pci_dev *pdev,
							  struct zpci_ccdf_err *ccdf)
{
	pci_ers_result_t ers_res = PCI_ERS_RESULT_DISCONNECT;
	struct zpci_dev *zdev = to_zpci(pdev);
	bool mediated_recovery = false;
	char *status_str = "success";
	struct pci_driver *driver;
	int rc;

	/*
	 * Ensure that the PCI function is not removed concurrently, no driver
	 * is unbound or probed and that userspace can't access its
	 * configuration space while we perform recovery.
	 */
	device_lock(&pdev->dev);
	if (pdev->error_state == pci_channel_io_perm_failure) {
		ers_res = PCI_ERS_RESULT_DISCONNECT;
		goto out_unlock;
	}
	pdev->error_state = pci_channel_io_frozen;

	driver = to_pci_driver(pdev->dev.driver);
	if (!is_driver_supported(driver)) {
		if (!driver) {
			pr_info("%s: Cannot be recovered because no driver is bound to the device\n",
				pci_name(pdev));
			status_str = "failed (no driver)";
		} else {
			pr_info("%s: The %s driver bound to the device does not support error recovery\n",
				pci_name(pdev),
				driver->name);
			status_str = "failed (no driver support)";
		}
		goto out_unlock;
	}

	rc = zpci_store_pci_error(pdev, ccdf);
	if (!rc || rc == -ENOMEM)
		mediated_recovery = true;

	ers_res = zpci_event_notify_error_detected(pdev, driver);
	if (ers_result_indicates_abort(ers_res)) {
		status_str = "failed (abort on detection)";
		goto out_unlock;
	}

	if (mediated_recovery) {
		pr_info("%s: Leaving recovery of pass-through device to user-space\n",
			pci_name(pdev));
		ers_res = PCI_ERS_RESULT_RECOVERED;
		status_str = "in progress";
		goto out_unlock;
	}

	if (ers_res != PCI_ERS_RESULT_NEED_RESET) {
		ers_res = zpci_event_do_error_state_clear(pdev, driver);
		if (ers_result_indicates_abort(ers_res)) {
			status_str = "failed (abort on MMIO enable)";
			goto out_unlock;
		}
	}

	if (ers_res == PCI_ERS_RESULT_NEED_RESET)
		ers_res = zpci_event_do_reset(pdev, driver);

	/*
	 * ers_res can be PCI_ERS_RESULT_NONE either because the driver
	 * decided to return it, indicating that it abstains from voting
	 * on how to recover, or because it didn't implement the callback.
	 * Both cases assume, that if there is nothing else causing a
	 * disconnect, we recovered successfully.
	 */
	if (ers_res == PCI_ERS_RESULT_NONE)
		ers_res = PCI_ERS_RESULT_RECOVERED;

	if (ers_res != PCI_ERS_RESULT_RECOVERED) {
		pci_uevent_ers(pdev, PCI_ERS_RESULT_DISCONNECT);
		pr_err("%s: Automatic recovery failed; operator intervention is required\n",
		       pci_name(pdev));
		status_str = "failed (driver can't recover)";
		goto out_unlock;
	}

	pr_info("%s: The device is ready to resume operations\n", pci_name(pdev));
	if (driver->err_handler->resume)
		driver->err_handler->resume(pdev);
	pci_uevent_ers(pdev, PCI_ERS_RESULT_RECOVERED);
out_unlock:
	device_unlock(&pdev->dev);
	zpci_report_status(zdev, "recovery", status_str);

	return ers_res;
}

/* zpci_event_io_failure - Report PCI channel failure state to driver
 * @pdev: PCI function for which to report
 * @es: PCI channel failure state to report
 */
static void zpci_event_io_failure(struct pci_dev *pdev, pci_channel_state_t es,
				  struct zpci_ccdf_err *ccdf)
{
	struct pci_driver *driver;

	pci_dev_lock(pdev);
	pdev->error_state = es;

	zpci_store_pci_error(pdev, ccdf);
	driver = to_pci_driver(pdev->dev.driver);
	if (driver && driver->err_handler && driver->err_handler->error_detected)
		driver->err_handler->error_detected(pdev, pdev->error_state);

	pci_dev_unlock(pdev);
}

static void __zpci_event_print_error(struct pci_dev *pdev, struct zpci_ccdf_err *ccdf)
{
	pr_err("%s: Event 0x%x reports an error for PCI function 0x%x\n",
	       pdev ? pci_name(pdev) : "n/a", ccdf->pec, ccdf->fid);
}

static void __zpci_event_error(struct zpci_ccdf_err *ccdf)
{
	struct zpci_dev *zdev = get_zdev_by_fid(ccdf->fid);
	struct pci_dev *pdev = NULL;
	pci_ers_result_t ers_res;
	u32 fh = 0;
	int rc;

	zpci_dbg(3, "err fid:%x, fh:%x, pec:%x\n",
		 ccdf->fid, ccdf->fh, ccdf->pec);
	zpci_err("error CCDF:\n");
	zpci_err_hex(ccdf, sizeof(*ccdf));

	if (!zdev)
		return __zpci_event_print_error(NULL, ccdf);

	mutex_lock(&zdev->state_lock);
	rc = clp_refresh_fh(zdev->fid, &fh);
	if (rc)
		goto no_pdev;
	if (!fh || ccdf->fh != fh) {
		/* Ignore events with stale handles */
		zpci_dbg(3, "err fid:%x, fh:%x (stale %x)\n",
			 ccdf->fid, fh, ccdf->fh);
		goto no_pdev;
	}
	zpci_update_fh(zdev, ccdf->fh);
	if (zdev->zbus->bus)
		pdev = pci_get_slot(zdev->zbus->bus, zdev->devfn);

	__zpci_event_print_error(pdev, ccdf);

	if (!pdev)
		goto no_pdev;

	switch (ccdf->pec) {
	case 0x002a: /* Error event concerns FMB */
	case 0x002b:
	case 0x002c:
		break;
	case 0x0040: /* Service Action or Error Recovery Failed */
	case 0x003b:
		zpci_event_io_failure(pdev, pci_channel_io_perm_failure, ccdf);
		break;
	default: /* PCI function left in the error state attempt to recover */
		ers_res = zpci_event_attempt_error_recovery(pdev, ccdf);
		if (ers_res != PCI_ERS_RESULT_RECOVERED)
			zpci_event_io_failure(pdev, pci_channel_io_perm_failure, ccdf);
		break;
	}
	pci_dev_put(pdev);
no_pdev:
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

static void zpci_event_reappear(struct zpci_dev *zdev)
{
	lockdep_assert_held(&zdev->state_lock);
	/*
	 * The zdev is in the reserved state. This means that it was presumed to
	 * go away but there are still undropped references. Now, the platform
	 * announced its availability again. Bring back the lingering zdev
	 * to standby. This is safe because we hold a temporary reference
	 * now so that it won't go away. Account for the re-appearance of the
	 * underlying device by incrementing the reference count.
	 */
	zdev->state = ZPCI_FN_STATE_STANDBY;
	zpci_zdev_get(zdev);
	zpci_dbg(1, "rea fid:%x, fh:%x\n", zdev->fid, zdev->fh);
}

static bool zpci_event_avail_any_device(struct zpci_ccdf_avail *ccdf)
{
	/* 0x0306 - No handle or fid stored */
	if (ccdf->pec != 0x0306)
		return false;
	/* 0x308 or 0x302 for multiple devices */
	zpci_remove_reserved_devices();
	zpci_scan_devices();
	return true;
}

static void zpci_event_avail_new_device(struct zpci_ccdf_avail *ccdf)
{
	struct zpci_dev *zdev;

	switch (ccdf->pec) {
	case 0x0301: /* Reserved|Standby -> Configured */
		zdev = zpci_create_device(ccdf->fid, ccdf->fh, ZPCI_FN_STATE_CONFIGURED);
		if (IS_ERR(zdev))
			break;
		if (zpci_add_device(zdev)) {
			kfree(zdev);
			break;
		}
		zpci_scan_configured_device(zdev, ccdf->fh);
		break;
	case 0x0302: /* Reserved -> Standby */
		zdev = zpci_create_device(ccdf->fid, ccdf->fh, ZPCI_FN_STATE_STANDBY);
		if (IS_ERR(zdev))
			break;
		if (zpci_add_device(zdev)) {
			kfree(zdev);
			break;
		}
		break;
	}
}

static void zpci_event_avail_existing_device(struct zpci_dev *zdev, struct zpci_ccdf_avail *ccdf)
{
	enum zpci_state state;

	switch (ccdf->pec) {
	case 0x0301: /* Reserved|Standby -> Configured */
		if (zdev->state == ZPCI_FN_STATE_RESERVED)
			zpci_event_reappear(zdev);
		/* the configuration request may be stale */
		else if (zdev->state != ZPCI_FN_STATE_STANDBY)
			break;
		zdev->state = ZPCI_FN_STATE_CONFIGURED;
		zpci_scan_configured_device(zdev, ccdf->fh);
		break;
	case 0x0302: /* Reserved -> Standby */
		if (zdev->state == ZPCI_FN_STATE_RESERVED)
			zpci_event_reappear(zdev);
		zpci_update_fh(zdev, ccdf->fh);
		break;
	case 0x0303: /* Deconfiguration requested */
		/* The event may have been queued before we configured
		 * the device.
		 */
		if (zdev->state != ZPCI_FN_STATE_CONFIGURED)
			break;
		zpci_update_fh(zdev, ccdf->fh);
		zpci_deconfigure_device(zdev);
		break;
	case 0x0304: /* Configured -> Standby|Reserved */
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
		break;
	case 0x0308: /* Standby -> Reserved */
		zpci_device_reserved(zdev);
		break;
	}
}

void zpci_event_availability(void *data)
{
	struct zpci_ccdf_avail *ccdf = data;
	struct zpci_dev *zdev;

	if (!zpci_is_enabled())
		return;
	zpci_dbg(3, "avl fid:%x, fh:%x, pec:%x\n",
		 ccdf->fid, ccdf->fh, ccdf->pec);
	if (zpci_event_avail_any_device(ccdf))
		return;
	zdev = get_zdev_by_fid(ccdf->fid);
	if (!zdev)
		return zpci_event_avail_new_device(ccdf);
	mutex_lock(&zdev->state_lock);
	zpci_event_avail_existing_device(zdev, ccdf);
	mutex_unlock(&zdev->state_lock);
	zpci_zdev_put(zdev);
}
