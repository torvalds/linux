// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include "xe_device.h"
#include "xe_drm_ras.h"
#include "xe_pm.h"
#include "xe_printk.h"
#include "xe_ras.h"
#include "xe_survivability_mode.h"
#include "xe_sysctrl.h"
#include "xe_sysctrl_event_types.h"
#include "xe_sysctrl_mailbox.h"
#include "xe_sysctrl_mailbox_types.h"

#define CORE_COMPUTE_UNCORR_TYPE	GENMASK(26, 25)
/*
 * Uncorrectable error type for core compute errors.
 * 0 - Correctable Error
 * 1 - Local Uncorrectable Error
 * 2 - Global Uncorrectable Error
 * 3 - Informational Error
 */
#define  GLOBAL_UNCORR_ERROR		2

/* Severity of detected errors  */
enum xe_ras_severity {
	XE_RAS_SEV_NOT_SUPPORTED = 0,
	XE_RAS_SEV_CORRECTABLE,
	XE_RAS_SEV_UNCORRECTABLE,
	XE_RAS_SEV_INFORMATIONAL,
	XE_RAS_SEV_MAX
};

/* Major IP blocks/components where errors can originate */
enum xe_ras_component {
	XE_RAS_COMP_NOT_SUPPORTED = 0,
	XE_RAS_COMP_DEVICE_MEMORY,
	XE_RAS_COMP_CORE_COMPUTE,
	XE_RAS_COMP_RESERVED,
	XE_RAS_COMP_PCIE,
	XE_RAS_COMP_FABRIC,
	XE_RAS_COMP_SOC_INTERNAL,
	XE_RAS_COMP_MAX
};

/* RAS response status codes */
enum xe_ras_response_status {
	XE_RAS_STATUS_SUCCESS = 0,
	XE_RAS_STATUS_INVALID_PARAM,
	XE_RAS_STATUS_OP_NOT_SUPPORTED,
	XE_RAS_STATUS_TIMEOUT,
	XE_RAS_STATUS_HARDWARE_FAILURE,
	XE_RAS_STATUS_INSUFFICIENT_RESOURCES,
	XE_RAS_STATUS_MAX
};

/* GPU health values */
enum xe_ras_health {
	XE_RAS_HEALTH_OK = 0,
	XE_RAS_HEALTH_WARNING,
	XE_RAS_HEALTH_CRITICAL,
	XE_RAS_HEALTH_MAX
};

static const char *const xe_ras_severities[] = {
	[XE_RAS_SEV_NOT_SUPPORTED]		= "Not Supported",
	[XE_RAS_SEV_CORRECTABLE]		= "Correctable Error",
	[XE_RAS_SEV_UNCORRECTABLE]		= "Uncorrectable Error",
	[XE_RAS_SEV_INFORMATIONAL]		= "Informational Error",
};
static_assert(ARRAY_SIZE(xe_ras_severities) == XE_RAS_SEV_MAX);

static const char *const xe_ras_components[] = {
	[XE_RAS_COMP_NOT_SUPPORTED]		= "Not Supported",
	[XE_RAS_COMP_DEVICE_MEMORY]		= "Device Memory",
	[XE_RAS_COMP_CORE_COMPUTE]		= "Core Compute",
	[XE_RAS_COMP_RESERVED]			= "Reserved",
	[XE_RAS_COMP_PCIE]			= "PCIe",
	[XE_RAS_COMP_FABRIC]			= "Fabric",
	[XE_RAS_COMP_SOC_INTERNAL]		= "SoC Internal",
};
static_assert(ARRAY_SIZE(xe_ras_components) == XE_RAS_COMP_MAX);

static const char * const gpu_health_states[] = {
	[XE_RAS_HEALTH_OK]		= "ok",
	[XE_RAS_HEALTH_WARNING]		= "warning",
	[XE_RAS_HEALTH_CRITICAL]	= "critical",
};
static_assert(ARRAY_SIZE(gpu_health_states) == XE_RAS_HEALTH_MAX);

static u8 drm_to_xe_ras_severity(u8 severity)
{
	switch (severity) {
	case DRM_XE_RAS_ERR_SEV_CORRECTABLE:
		return XE_RAS_SEV_CORRECTABLE;
	case DRM_XE_RAS_ERR_SEV_UNCORRECTABLE:
		return XE_RAS_SEV_UNCORRECTABLE;
	default:
		return XE_RAS_SEV_NOT_SUPPORTED;
	}
}

static u8 drm_to_xe_ras_component(u8 component)
{
	switch (component) {
	case DRM_XE_RAS_ERR_COMP_CORE_COMPUTE:
		return XE_RAS_COMP_CORE_COMPUTE;
	case DRM_XE_RAS_ERR_COMP_SOC_INTERNAL:
		return XE_RAS_COMP_SOC_INTERNAL;
	case DRM_XE_RAS_ERR_COMP_DEVICE_MEMORY:
		return XE_RAS_COMP_DEVICE_MEMORY;
	case DRM_XE_RAS_ERR_COMP_PCIE:
		return XE_RAS_COMP_PCIE;
	case DRM_XE_RAS_ERR_COMP_FABRIC:
		return XE_RAS_COMP_FABRIC;
	default:
		return XE_RAS_COMP_NOT_SUPPORTED;
	}
}

static int ras_status_to_errno(u32 status)
{
	switch (status) {
	case XE_RAS_STATUS_SUCCESS:
		return 0;
	case XE_RAS_STATUS_INVALID_PARAM:
		return -EINVAL;
	case XE_RAS_STATUS_OP_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case XE_RAS_STATUS_TIMEOUT:
		return -ETIMEDOUT;
	case XE_RAS_STATUS_HARDWARE_FAILURE:
		return -EIO;
	case XE_RAS_STATUS_INSUFFICIENT_RESOURCES:
		return -ENOSPC;
	default:
		return -EPROTO;
	}
}

static inline const char *sev_to_str(u8 severity)
{
	if (severity >= XE_RAS_SEV_MAX)
		severity = XE_RAS_SEV_NOT_SUPPORTED;

	return xe_ras_severities[severity];
}

static inline const char *comp_to_str(u8 component)
{
	if (component >= XE_RAS_COMP_MAX)
		component = XE_RAS_COMP_NOT_SUPPORTED;

	return xe_ras_components[component];
}

static struct pci_dev *find_usp_dev(struct pci_dev *pdev)
{
	struct pci_dev *vsp;

	/*
	 * Device Hierarchy:
	 *
	 * Upstream Switch Port (USP) --> Virtual Switch Port (VSP) --> SGunit (GPU endpoint)
	 */
	vsp = pci_upstream_bridge(pdev);
	if (!vsp)
		return NULL;

	return pci_upstream_bridge(vsp);
}

static void ras_usp_aer_init(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct pci_dev *usp;
	u16 aer_cap;
	u32 status;

	usp = find_usp_dev(pdev);
	if (!usp)
		return;

	aer_cap = pci_find_ext_capability(usp, PCI_EXT_CAP_ID_ERR);
	if (!aer_cap) {
		dev_warn(&usp->dev, "AER capability unavailable\n");
		return;
	}

	/*
	 * Clear any stale Uncorrectable Internal Error Status event in Uncorrectable Error
	 * Status Register.
	 */
	pci_read_config_dword(usp, aer_cap + PCI_ERR_UNCOR_STATUS, &status);
	if (status & PCI_ERR_UNC_INTN)
		pci_write_config_dword(usp, aer_cap + PCI_ERR_UNCOR_STATUS, PCI_ERR_UNC_INTN);

	/*
	 * All errors are steered to USP which is a PCIe AER Compliant device.
	 * Downgrade all the errors to non-fatal to prevent PCIe bus driver
	 * from triggering a Secondary Bus Reset (SBR). This allows error
	 * detection, containment and recovery in the driver.
	 *
	 * The Uncorrectable Error Severity Register has the 'Uncorrectable
	 * Internal Error Severity' set to fatal by default. Set this to
	 * non-fatal and unmask the error.
	 */

	/* Downgrade Uncorrectable Internal Error to non-fatal */
	pci_clear_and_set_config_dword(usp, aer_cap + PCI_ERR_UNCOR_SEVER, PCI_ERR_UNC_INTN, 0);

	/* Unmask Uncorrectable Internal Error */
	pci_clear_and_set_config_dword(usp, aer_cap + PCI_ERR_UNCOR_MASK, PCI_ERR_UNC_INTN, 0);

	pci_save_state(usp);
	dev_dbg(&usp->dev, "Uncorrectable Internal Errors downgraded and unmasked\n");
}

static u8 handle_core_compute_errors(struct xe_ras_error_array *arr)
{
	struct xe_ras_compute_error *error_info = (void *)arr->details;
	u8 uncorr_type;

	uncorr_type = FIELD_GET(CORE_COMPUTE_UNCORR_TYPE, error_info->log_header);

	/* Request a reset if error is global */
	if (uncorr_type == GLOBAL_UNCORR_ERROR)
		return XE_RAS_RECOVERY_ACTION_RESET;

	/*
	 * No action needed for other errors.
	 * Local errors are recovered using an engine reset by GuC.
	 */
	return XE_RAS_RECOVERY_ACTION_RECOVERED;
}

static u8 handle_soc_internal_errors(struct xe_device *xe, struct xe_ras_error_array *arr)
{
	struct xe_ras_soc_error *info = (void *)arr->details;
	struct xe_ras_soc_error_source *source = &info->source;
	struct xe_ras_error_class *counter = &arr->counter;

	if (source->csc) {
		struct xe_ras_csc_error *csc_error = (void *)info->details;

		/*
		 * CSC uncorrectable errors are classified as hardware errors and firmware errors.
		 * CSC firmware errors are critical errors that can be recovered only by firmware
		 * update via SPI driver. On a CSC firmware error, PCODE enables FDO mode and sets
		 * the bit in the capability register. On receiving this error, the driver enables
		 * runtime survivability mode which notifies userspace that a firmware update
		 * is required.
		 */
		if (csc_error->hec_fw_error) {
			xe_err(xe, "[RAS]: CSC %s detected: 0x%x\n",
			       sev_to_str(counter->common.severity),
			       csc_error->hec_fw_error);
			xe_survivability_mode_runtime_enable(xe);
			return XE_RAS_RECOVERY_ACTION_DISCONNECT;
		}
	} else if (source->ieh) {
		struct xe_ras_ieh_error *ieh_error = (void *)info->details;

		if (ieh_error->global_error_status & XE_RAS_SOC_IEH_PUNIT) {
			xe_err(xe, "[RAS]: PUNIT %s detected: 0x%x\n",
			       sev_to_str(counter->common.severity),
			       ieh_error->global_error_status);
			/* TODO: Add PUNIT error handling */
			return XE_RAS_RECOVERY_ACTION_DISCONNECT;
		}
	}

	/* For other SoC internal errors, request a reset as recovery mechanism */
	return XE_RAS_RECOVERY_ACTION_RESET;
}

static u8 handle_device_memory_errors(struct xe_device *xe, struct xe_ras_error_array *arr)
{
	struct xe_ras_memory_error *info = (void *)arr->details;

	/*
	 * For memory errors, the recovery action depends on the error category
	 *
	 * TODO: Double-bit ECC errors: Page offlining
	 * Poison and data parity errors: Log only
	 * For any other memory errors, request a reset as recovery mechanism
	 */
	switch (info->category) {
	case XE_RAS_MEMORY_POISON:
		xe_info(xe, "[RAS]: Poison error detected\n");
		break;
	case XE_RAS_MEMORY_DATA_PARITY:
		xe_info(xe, "[RAS]: Data parity error detected\n");
		break;
	case XE_RAS_MEMORY_DB_ECC:
		xe_info(xe, "[RAS]: Double-bit ECC error detected at sw address 0x%llx\n",
			info->sw_address);
		/* TODO: Add page offlining for Double-bit ECC error */
		fallthrough;
	default:
		return XE_RAS_RECOVERY_ACTION_RESET;
	}

	return XE_RAS_RECOVERY_ACTION_RECOVERED;
}

void xe_ras_counter_threshold_crossed(struct xe_device *xe,
				      struct xe_sysctrl_event_response *response)
{
	struct xe_ras_threshold_crossed *pending = (void *)&response->data;
	struct xe_ras_error_class *errors = pending->counters;
	u32 id, ncounters = pending->ncounters;

	BUILD_BUG_ON(sizeof(response->data) < sizeof(*pending));
	xe_device_assert_mem_access(xe);

	if (!ncounters || ncounters > XE_RAS_NUM_COUNTERS)
		xe_err(xe, "sysctrl: unexpected counter threshold crossed %u\n", ncounters);
	else
		xe_warn(xe, "[RAS]: counter threshold crossed, %u new errors\n", ncounters);

	for (id = 0; id < ncounters && id < XE_RAS_NUM_COUNTERS; id++) {
		u8 severity, component;

		severity = errors[id].common.severity;
		component = errors[id].common.component;

		xe_warn(xe, "[RAS]: %s %s detected\n",
			comp_to_str(component), sev_to_str(severity));
	}
}

static int get_counter(struct xe_device *xe, struct xe_ras_error_class *counter, u32 *value)
{
	struct xe_ras_get_counter_response response = {0};
	struct xe_ras_get_counter_request request = {0};
	struct xe_sysctrl_mailbox_command command = {0};
	struct xe_ras_error_common *common;
	size_t rlen;
	int ret;

	request.counter = *counter;

	xe_sysctrl_create_command(&command, XE_SYSCTRL_GROUP_GFSP, XE_SYSCTRL_CMD_GET_COUNTER,
				  &request, sizeof(request), &response, sizeof(response));

	ret = xe_sysctrl_send_command(&xe->sc, &command, &rlen);
	if (ret) {
		xe_err(xe, "sysctrl: failed to get counter %d\n", ret);
		return ret;
	}

	if (rlen != sizeof(response)) {
		xe_err(xe, "sysctrl: unexpected get counter response length %zu (expected %zu)\n",
		       rlen, sizeof(response));
		return -EIO;
	}

	common = &response.counter.common;
	*value = response.value;

	xe_dbg(xe, "[RAS]: get counter %u for %s %s\n", *value, comp_to_str(common->component),
	       sev_to_str(common->severity));

	return 0;
}

/**
 * xe_ras_process_errors() - Process and contain hardware errors
 * @xe: xe device instance
 *
 * Get error details from system controller and return recovery
 * method.
 *
 * Returns: recovery action to be taken
 */
enum xe_ras_recovery_action xe_ras_process_errors(struct xe_device *xe)
{
	struct xe_sysctrl_mailbox_command command = {0};
	enum xe_ras_recovery_action final_action;
	u32 remaining = XE_SYSCTRL_FLOOD_LIMIT;
	struct xe_ras_get_soc_error response;
	size_t rlen;
	int ret;

	if (!xe->info.has_sysctrl)
		return XE_RAS_RECOVERY_ACTION_RESET;

	/* Default action */
	final_action = XE_RAS_RECOVERY_ACTION_RECOVERED;

	xe_sysctrl_create_command(&command, XE_SYSCTRL_GROUP_GFSP, XE_SYSCTRL_CMD_GET_SOC_ERROR,
				  NULL, 0, &response, sizeof(response));

	do {
		memset(&response, 0, sizeof(response));

		ret = xe_sysctrl_send_command(&xe->sc, &command, &rlen);
		if (ret) {
			xe_err(xe, "sysctrl: failed to get soc error %d\n", ret);
			goto err;
		}

		if (rlen != sizeof(response)) {
			xe_err(xe, "sysctrl: unexpected get soc error response length %zu (expected %zu)\n",
			       rlen, sizeof(response));
			goto err;
		}

		/* Report if number of errors exceeds the maximum errors supported */
		if (response.num_errors > XE_RAS_NUM_ERROR_ARR)
			xe_err(xe, "sysctrl: number of errors received %d out of bound (%d)\n",
			       response.num_errors, XE_RAS_NUM_ERROR_ARR);

		for (int i = 0; i < response.num_errors && i < XE_RAS_NUM_ERROR_ARR; i++) {
			struct xe_ras_error_array *arr = &response.arr[i];
			enum xe_ras_recovery_action action;
			u8 component, severity;

			component = arr->counter.common.component;
			severity = arr->counter.common.severity;

			xe_info(xe, "[RAS]: %s %s detected\n", comp_to_str(component),
				sev_to_str(severity));

			switch (component) {
			case XE_RAS_COMP_CORE_COMPUTE:
				action = handle_core_compute_errors(arr);
				break;
			case XE_RAS_COMP_SOC_INTERNAL:
				action = handle_soc_internal_errors(xe, arr);
				break;
			case XE_RAS_COMP_DEVICE_MEMORY:
				action = handle_device_memory_errors(xe, arr);
				break;
			default:
				/* For any other component, reset */
				action = XE_RAS_RECOVERY_ACTION_RESET;
				break;
			}

			/* Process and log all errors and then trigger highest recovery action */
			if (action > final_action)
				final_action = action;
		}

		/* Treat flooding as a system controller error */
		if (!--remaining) {
			xe_err(xe, "[RAS]: sysctrl: get soc error response flooding\n");
			goto err;
		}

	} while (response.additional_errors);

	return final_action;

err:
	return XE_RAS_RECOVERY_ACTION_RESET;
}

/**
 * xe_ras_get_counter() - Get error counter value
 * @xe: Xe device instance
 * @severity: Error severity to be queried (&enum drm_xe_ras_error_severity)
 * @component: Error component to be queried (&enum drm_xe_ras_error_component)
 * @value: Counter value
 *
 * This function retrieves the value of a specific error counter based on
 * the error severity and component.
 *
 * Return: 0 on success, negative error code on failure.
 */
int xe_ras_get_counter(struct xe_device *xe, u8 severity, u8 component, u32 *value)
{
	struct xe_ras_error_class counter = {0};

	counter.common.severity = drm_to_xe_ras_severity(severity);
	counter.common.component = drm_to_xe_ras_component(component);

	guard(xe_pm_runtime)(xe);
	return get_counter(xe, &counter, value);
}

/**
 * xe_ras_clear_counter() - Clear error counter value
 * @xe: Xe device instance
 * @severity: Error severity to be cleared (&enum drm_xe_ras_error_severity)
 * @component: Error component to be cleared (&enum drm_xe_ras_error_component)
 *
 * This function clears the value of a specific error counter based on
 * the error severity and component.
 *
 * Return: 0 on success, negative error code on failure.
 */
int xe_ras_clear_counter(struct xe_device *xe, u8 severity, u8 component)
{
	struct xe_ras_clear_counter_response response = {0};
	struct xe_ras_clear_counter_request request = {0};
	struct xe_sysctrl_mailbox_command command = {0};
	struct xe_ras_error_class *counter;
	size_t rlen;
	int ret;

	counter = &request.counter;
	counter->common.severity = drm_to_xe_ras_severity(severity);
	counter->common.component = drm_to_xe_ras_component(component);

	xe_sysctrl_create_command(&command, XE_SYSCTRL_GROUP_GFSP, XE_SYSCTRL_CMD_CLEAR_COUNTER,
				  &request, sizeof(request), &response, sizeof(response));

	guard(xe_pm_runtime)(xe);
	ret = xe_sysctrl_send_command(&xe->sc, &command, &rlen);
	if (ret) {
		xe_err(xe, "sysctrl: failed to clear counter %d\n", ret);
		return ret;
	}

	if (rlen != sizeof(response)) {
		xe_err(xe, "sysctrl: unexpected clear counter response length %zu (expected %zu)\n",
		       rlen, sizeof(response));
		return -EIO;
	}

	ret = ras_status_to_errno(response.status);
	if (ret) {
		xe_err(xe, "sysctrl: clear counter command failed with status %#x\n",
		       response.status);
		return ret;
	}

	counter = &response.counter;

	xe_dbg(xe, "[RAS]: clear counter for %s %s\n", comp_to_str(counter->common.component),
	       sev_to_str(counter->common.severity));

	return 0;
}

static ssize_t gpu_health_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct xe_ras_get_health_response response = {0};
	struct xe_sysctrl_mailbox_command command = {0};
	struct xe_ras_get_health_request request = {0};
	struct xe_device *xe = kdev_to_xe_device(dev);
	const char *health;
	size_t rlen;
	int ret;

	xe_sysctrl_create_command(&command, XE_SYSCTRL_GROUP_GFSP, XE_SYSCTRL_CMD_GET_HEALTH,
				  &request, sizeof(request), &response, sizeof(response));
	guard(xe_pm_runtime)(xe);
	ret = xe_sysctrl_send_command(&xe->sc, &command, &rlen);
	if (ret) {
		xe_err(xe, "sysctrl: failed to get health %d\n", ret);
		return ret;
	}

	if (rlen != sizeof(response)) {
		xe_err(xe, "sysctrl: unexpected get health response length %zu (expected %zu)\n",
		       rlen, sizeof(response));
		return -EIO;
	}
	if (response.health >= XE_RAS_HEALTH_MAX) {
		xe_err(xe, "sysctrl: invalid health state %u\n",
		       response.health);
		return -EIO;
	}

	health = gpu_health_states[response.health];

	xe_dbg(xe, "[RAS]: get health: %s\n", health);

	return sysfs_emit(buf, "%s\n", health);
}

static ssize_t gpu_health_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct xe_ras_set_health_response response = {0};
	struct xe_sysctrl_mailbox_command command = {0};
	struct xe_ras_set_health_request request = {0};
	struct xe_device *xe = kdev_to_xe_device(dev);
	const char *health;
	size_t rlen;
	int state;
	int ret;

	state = sysfs_match_string(gpu_health_states, buf);
	if (state < 0)
		return -EINVAL;

	request.health = state;

	xe_sysctrl_create_command(&command, XE_SYSCTRL_GROUP_GFSP, XE_SYSCTRL_CMD_SET_HEALTH,
				  &request, sizeof(request), &response, sizeof(response));
	guard(xe_pm_runtime)(xe);
	ret = xe_sysctrl_send_command(&xe->sc, &command, &rlen);
	if (ret) {
		xe_err(xe, "sysctrl: failed to set health %d\n", ret);
		return ret;
	}

	if (rlen != sizeof(response)) {
		xe_err(xe, "sysctrl: unexpected set health response length %zu (expected %zu)\n",
		       rlen, sizeof(response));
		return -EIO;
	}

	ret = ras_status_to_errno(response.status);
	if (ret) {
		xe_err(xe, "sysctrl: set health command failed with status %#x\n",
		       response.status);
		return ret;
	}

	if (response.health >= XE_RAS_HEALTH_MAX) {
		xe_err(xe, "sysctrl: invalid health state %u\n",
		       response.health);
		return -EIO;
	}

	health = gpu_health_states[response.health];

	xe_dbg(xe, "[RAS]: set health: %s\n", health);

	return count;
}
static DEVICE_ATTR_RW(gpu_health);

static struct attribute *gpu_health_attrs[] = {
	&dev_attr_gpu_health.attr,
	NULL
};

/**
 * DOC: GPU Health Indicator
 *
 * On Intel Xe platforms that support the gpu health indicator interface,
 * the driver exposes this sysfs attribute for in-band access to the gpu
 * health state::
 *
 *     /sys/bus/pci/devices/<device>/gpu_health
 *
 * Reading the attribute is available to all users and returns a single
 * line containing the current gpu health state, whereas writing is
 * restricted to administrative users and updates the state to one of the
 * valid values.
 *
 * Management tools and administrators use this interface to query the
 * current gpu health state (e.g. for telemetry/monitoring) and to
 * update it - for example, to mark the gpu as ``warning`` or ``critical``
 * after diagnostics, or reset it back to ``ok`` once remediated.
 *
 * The valid values for the gpu health state are:
 *
 * - ``ok``
 *     The gpu is healthy and operating within normal parameters.
 *
 * - ``warning``
 *     The gpu is experiencing minor issues but remains operational.
 *
 * - ``critical``
 *     The gpu is in a critical state and may not be operational.
 *
 * See Documentation/ABI/testing/sysfs-driver-intel-xe-ras for the ABI
 * specification.
 */
static const struct attribute_group gpu_health_group = {
	.attrs = gpu_health_attrs,
};

/**
 * xe_ras_init - Initialize Xe RAS
 * @xe: xe device instance
 *
 * Initialize Xe RAS
 */
void xe_ras_init(struct xe_device *xe)
{
	int ret;

	if (!xe->info.has_drm_ras)
		return;

	xe_drm_ras_init(xe);

	if (!xe->info.has_sysctrl)
		return;

	if (IS_ENABLED(CONFIG_PCIEAER))
		ras_usp_aer_init(xe);

	/*
	 * During probe, process and log any errors detected by firmware while the driver was not
	 * loaded. Critical errors such as Punit and CSC are reported through Pcode init failure,
	 * causing the driver to enter survivability mode.
	 */
	xe_ras_process_errors(xe);
	ret = devm_device_add_group(xe->drm.dev, &gpu_health_group);
	if (ret)
		xe_err(xe, "Failed to create GPU health sysfs, err=%d\n", ret);
}
