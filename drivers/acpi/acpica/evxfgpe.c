// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evxfgpe - External Interfaces for General Purpose Events (GPEs)
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#define EXPORT_ACPI_INTERFACES

#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxfgpe")

#if (!ACPI_REDUCED_HARDWARE)	/* Entire module */
/*******************************************************************************
 *
 * FUNCTION:    acpi_update_all_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Complete GPE initialization and enable all GPEs that have
 *              associated _Lxx or _Exx methods and are not pointed to by any
 *              device _PRW methods (this indicates that these GPEs are
 *              generally intended for system or device wakeup. Such GPEs
 *              have to be enabled directly when the devices whose _PRW
 *              methods point to them are set up for wakeup signaling.)
 *
 * NOTE: Should be called after any GPEs are added to the system. Primarily,
 * after the system _PRW methods have been run, but also after a GPE Block
 * Device has been added or if any new GPE methods have been added via a
 * dynamic table load.
 *
 ******************************************************************************/

acpi_status acpi_update_all_gpes(void)
{
	acpi_status status;
	u8 is_polling_needed = FALSE;

	ACPI_FUNCTION_TRACE(acpi_update_all_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (acpi_gbl_all_gpes_initialized) {
		goto unlock_and_exit;
	}

	status = acpi_ev_walk_gpe_list(acpi_ev_initialize_gpe_block,
				       &is_polling_needed);
	if (ACPI_SUCCESS(status)) {
		acpi_gbl_all_gpes_initialized = TRUE;
	}

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	if (is_polling_needed && acpi_gbl_all_gpes_initialized) {

		/* Poll GPEs to handle already triggered events */

		acpi_ev_gpe_detect(acpi_gbl_gpe_xrupt_list_head);
	}
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_update_all_gpes)

/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_gpe
 *
 * PARAMETERS:  gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a reference to a GPE. On the first reference, the GPE is
 *              hardware-enabled.
 *
 ******************************************************************************/
acpi_status acpi_enable_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_enable_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/*
	 * Ensure that we have a valid GPE number and that there is some way
	 * of handling the GPE (handler or a GPE method). In other words, we
	 * won't allow a valid GPE to be enabled if there is no way to handle it.
	 */
	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {
		if (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) !=
		    ACPI_GPE_DISPATCH_NONE) {
			status = acpi_ev_add_gpe_reference(gpe_event_info, TRUE);
			if (ACPI_SUCCESS(status) &&
			    ACPI_GPE_IS_POLLING_NEEDED(gpe_event_info)) {

				/* Poll edge-triggered GPEs to handle existing events */

				acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
				(void)acpi_ev_detect_gpe(gpe_device,
							 gpe_event_info,
							 gpe_number);
				flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
			}
		} else {
			status = AE_NO_HANDLER;
		}
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_enable_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_disable_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a reference to a GPE. When the last reference is
 *              removed, only then is the GPE disabled (for runtime GPEs), or
 *              the GPE mask bit disabled (for wake GPEs)
 *
 ******************************************************************************/

acpi_status acpi_disable_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_disable_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {
		status = acpi_ev_remove_gpe_reference(gpe_event_info) ;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_disable_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_gpe
 *
 * PARAMETERS:  gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *              action              - ACPI_GPE_ENABLE or ACPI_GPE_DISABLE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable or disable an individual GPE. This function bypasses
 *              the reference count mechanism used in the acpi_enable_gpe(),
 *              acpi_disable_gpe() interfaces.
 *              This API is typically used by the GPE raw handler mode driver
 *              to switch between the polling mode and the interrupt mode after
 *              the driver has enabled the GPE.
 *              The APIs should be invoked in this order:
 *               acpi_enable_gpe()            <- Ensure the reference count > 0
 *               acpi_set_gpe(ACPI_GPE_DISABLE) <- Enter polling mode
 *               acpi_set_gpe(ACPI_GPE_ENABLE) <- Leave polling mode
 *               acpi_disable_gpe()           <- Decrease the reference count
 *
 * Note: If a GPE is shared by 2 silicon components, then both the drivers
 *       should support GPE polling mode or disabling the GPE for long period
 *       for one driver may break the other. So use it with care since all
 *       firmware _Lxx/_Exx handlers currently rely on the GPE interrupt mode.
 *
 ******************************************************************************/
acpi_status acpi_set_gpe(acpi_handle gpe_device, u32 gpe_number, u8 action)
{
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_set_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Perform the action */

	switch (action) {
	case ACPI_GPE_ENABLE:

		status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_ENABLE);
		gpe_event_info->disable_for_dispatch = FALSE;
		break;

	case ACPI_GPE_DISABLE:

		status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_DISABLE);
		gpe_event_info->disable_for_dispatch = TRUE;
		break;

	default:

		status = AE_BAD_PARAMETER;
		break;
	}

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_set_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_mask_gpe
 *
 * PARAMETERS:  gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *              is_masked           - Whether the GPE is masked or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unconditionally mask/unmask the an individual GPE, ex., to
 *              prevent a GPE flooding.
 *
 ******************************************************************************/
acpi_status acpi_mask_gpe(acpi_handle gpe_device, u32 gpe_number, u8 is_masked)
{
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_mask_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ev_mask_gpe(gpe_event_info, is_masked);

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_mask_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_mark_gpe_for_wake
 *
 * PARAMETERS:  gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Mark a GPE as having the ability to wake the system. Simply
 *              sets the ACPI_GPE_CAN_WAKE flag.
 *
 * Some potential callers of acpi_setup_gpe_for_wake may know in advance that
 * there won't be any notify handlers installed for device wake notifications
 * from the given GPE (one example is a button GPE in Linux). For these cases,
 * acpi_mark_gpe_for_wake should be used instead of acpi_setup_gpe_for_wake.
 * This will set the ACPI_GPE_CAN_WAKE flag for the GPE without trying to
 * setup implicit wake notification for it (since there's no handler method).
 *
 ******************************************************************************/
acpi_status acpi_mark_gpe_for_wake(acpi_handle gpe_device, u32 gpe_number)
{
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_status status = AE_BAD_PARAMETER;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_mark_gpe_for_wake);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {

		/* Mark the GPE as a possible wake event */

		gpe_event_info->flags |= ACPI_GPE_CAN_WAKE;
		status = AE_OK;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_mark_gpe_for_wake)

/*******************************************************************************
 *
 * FUNCTION:    acpi_setup_gpe_for_wake
 *
 * PARAMETERS:  wake_device         - Device associated with the GPE (via _PRW)
 *              gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Mark a GPE as having the ability to wake the system. This
 *              interface is intended to be used as the host executes the
 *              _PRW methods (Power Resources for Wake) in the system tables.
 *              Each _PRW appears under a Device Object (The wake_device), and
 *              contains the info for the wake GPE associated with the
 *              wake_device.
 *
 ******************************************************************************/
acpi_status
acpi_setup_gpe_for_wake(acpi_handle wake_device,
			acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status;
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_namespace_node *device_node;
	struct acpi_gpe_notify_info *notify;
	struct acpi_gpe_notify_info *new_notify;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_setup_gpe_for_wake);

	/* Parameter Validation */

	if (!wake_device) {
		/*
		 * By forcing wake_device to be valid, we automatically enable the
		 * implicit notify feature on all hosts.
		 */
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Handle root object case */

	if (wake_device == ACPI_ROOT_OBJECT) {
		device_node = acpi_gbl_root_node;
	} else {
		device_node =
		    ACPI_CAST_PTR(struct acpi_namespace_node, wake_device);
	}

	/* Validate wake_device is of type Device */

	if (device_node->type != ACPI_TYPE_DEVICE) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Allocate a new notify object up front, in case it is needed.
	 * Memory allocation while holding a spinlock is a big no-no
	 * on some hosts.
	 */
	new_notify = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_gpe_notify_info));
	if (!new_notify) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * If there is no method or handler for this GPE, then the
	 * wake_device will be notified whenever this GPE fires. This is
	 * known as an "implicit notify". Note: The GPE is assumed to be
	 * level-triggered (for windows compatibility).
	 */
	if (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	    ACPI_GPE_DISPATCH_NONE) {
		/*
		 * This is the first device for implicit notify on this GPE.
		 * Just set the flags here, and enter the NOTIFY block below.
		 */
		gpe_event_info->flags =
		    (ACPI_GPE_DISPATCH_NOTIFY | ACPI_GPE_LEVEL_TRIGGERED);
	} else if (gpe_event_info->flags & ACPI_GPE_AUTO_ENABLED) {
		/*
		 * A reference to this GPE has been added during the GPE block
		 * initialization, so drop it now to prevent the GPE from being
		 * permanently enabled and clear its ACPI_GPE_AUTO_ENABLED flag.
		 */
		(void)acpi_ev_remove_gpe_reference(gpe_event_info);
		gpe_event_info->flags &= ~ACPI_GPE_AUTO_ENABLED;
	}

	/*
	 * If we already have an implicit notify on this GPE, add
	 * this device to the notify list.
	 */
	if (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	    ACPI_GPE_DISPATCH_NOTIFY) {

		/* Ensure that the device is not already in the list */

		notify = gpe_event_info->dispatch.notify_list;
		while (notify) {
			if (notify->device_node == device_node) {
				status = AE_ALREADY_EXISTS;
				goto unlock_and_exit;
			}
			notify = notify->next;
		}

		/* Add this device to the notify list for this GPE */

		new_notify->device_node = device_node;
		new_notify->next = gpe_event_info->dispatch.notify_list;
		gpe_event_info->dispatch.notify_list = new_notify;
		new_notify = NULL;
	}

	/* Mark the GPE as a possible wake event */

	gpe_event_info->flags |= ACPI_GPE_CAN_WAKE;
	status = AE_OK;

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

	/* Delete the notify object if it was not used above */

	if (new_notify) {
		ACPI_FREE(new_notify);
	}
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_setup_gpe_for_wake)

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_gpe_wake_mask
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *              action              - Enable or Disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set or clear the GPE's wakeup enable mask bit. The GPE must
 *              already be marked as a WAKE GPE.
 *
 ******************************************************************************/

acpi_status
acpi_set_gpe_wake_mask(acpi_handle gpe_device, u32 gpe_number, u8 action)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_register_info *gpe_register_info;
	acpi_cpu_flags flags;
	u32 register_bit;

	ACPI_FUNCTION_TRACE(acpi_set_gpe_wake_mask);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/*
	 * Ensure that we have a valid GPE number and that this GPE is in
	 * fact a wake GPE
	 */
	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	if (!(gpe_event_info->flags & ACPI_GPE_CAN_WAKE)) {
		status = AE_TYPE;
		goto unlock_and_exit;
	}

	gpe_register_info = gpe_event_info->register_info;
	if (!gpe_register_info) {
		status = AE_NOT_EXIST;
		goto unlock_and_exit;
	}

	register_bit = acpi_hw_get_gpe_register_bit(gpe_event_info);

	/* Perform the action */

	switch (action) {
	case ACPI_GPE_ENABLE:

		ACPI_SET_BIT(gpe_register_info->enable_for_wake,
			     (u8)register_bit);
		break;

	case ACPI_GPE_DISABLE:

		ACPI_CLEAR_BIT(gpe_register_info->enable_for_wake,
			       (u8)register_bit);
		break;

	default:

		ACPI_ERROR((AE_INFO, "%u, Invalid action", action));
		status = AE_BAD_PARAMETER;
		break;
	}

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_set_gpe_wake_mask)

/*******************************************************************************
 *
 * FUNCTION:    acpi_clear_gpe
 *
 * PARAMETERS:  gpe_device      - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number      - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear an ACPI event (general purpose)
 *
 ******************************************************************************/
acpi_status acpi_clear_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_clear_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_hw_clear_gpe(gpe_event_info);

      unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_clear_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_gpe_status
 *
 * PARAMETERS:  gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *              event_status        - Where the current status of the event
 *                                    will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the current status of a GPE (signalled/not_signalled)
 *
 ******************************************************************************/
acpi_status
acpi_get_gpe_status(acpi_handle gpe_device,
		    u32 gpe_number, acpi_event_status *event_status)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_get_gpe_status);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Obtain status on the requested GPE number */

	status = acpi_hw_get_gpe_status(gpe_event_info, event_status);

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_gpe_status)

/*******************************************************************************
 *
 * FUNCTION:    acpi_gispatch_gpe
 *
 * PARAMETERS:  gpe_device          - Parent GPE Device. NULL for GPE0/GPE1
 *              gpe_number          - GPE level within the GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect and dispatch a General Purpose Event to either a function
 *              (e.g. EC) or method (e.g. _Lxx/_Exx) handler.
 *
 ******************************************************************************/
u32 acpi_dispatch_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	ACPI_FUNCTION_TRACE(acpi_dispatch_gpe);

	return acpi_ev_detect_gpe(gpe_device, NULL, gpe_number);
}

ACPI_EXPORT_SYMBOL(acpi_dispatch_gpe)

/*******************************************************************************
 *
 * FUNCTION:    acpi_finish_gpe
 *
 * PARAMETERS:  gpe_device          - Namespace node for the GPE Block
 *                                    (NULL for FADT defined GPEs)
 *              gpe_number          - GPE level within the GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear and conditionally re-enable a GPE. This completes the GPE
 *              processing. Intended for use by asynchronous host-installed
 *              GPE handlers. The GPE is only re-enabled if the enable_for_run bit
 *              is set in the GPE info.
 *
 ******************************************************************************/
acpi_status acpi_finish_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_finish_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ev_finish_gpe(gpe_event_info);

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_finish_gpe)

/******************************************************************************
 *
 * FUNCTION:    acpi_disable_all_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable and clear all GPEs in all GPE blocks
 *
 ******************************************************************************/

acpi_status acpi_disable_all_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_disable_all_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_disable_all_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_disable_all_gpes)

/******************************************************************************
 *
 * FUNCTION:    acpi_enable_all_runtime_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "runtime" GPEs, in all GPE blocks
 *
 ******************************************************************************/

acpi_status acpi_enable_all_runtime_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enable_all_runtime_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_enable_all_runtime_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_enable_all_runtime_gpes)

/******************************************************************************
 *
 * FUNCTION:    acpi_enable_all_wakeup_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "wakeup" GPEs and disable all of the other GPEs, in
 *              all GPE blocks.
 *
 ******************************************************************************/
acpi_status acpi_enable_all_wakeup_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enable_all_wakeup_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_enable_all_wakeup_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_enable_all_wakeup_gpes)

/******************************************************************************
 *
 * FUNCTION:    acpi_any_gpe_status_set
 *
 * PARAMETERS:  gpe_skip_number      - Number of the GPE to skip
 *
 * RETURN:      Whether or not the status bit is set for any GPE
 *
 * DESCRIPTION: Check the status bits of all enabled GPEs, except for the one
 *              represented by the "skip" argument, and return TRUE if any of
 *              them is set or FALSE otherwise.
 *
 ******************************************************************************/
u32 acpi_any_gpe_status_set(u32 gpe_skip_number)
{
	acpi_status status;
	acpi_handle gpe_device;
	u8 ret;

	ACPI_FUNCTION_TRACE(acpi_any_gpe_status_set);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return (FALSE);
	}

	status = acpi_get_gpe_device(gpe_skip_number, &gpe_device);
	if (ACPI_FAILURE(status)) {
		gpe_device = NULL;
	}

	ret = acpi_hw_check_all_gpes(gpe_device, gpe_skip_number);
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return (ret);
}

ACPI_EXPORT_SYMBOL(acpi_any_gpe_status_set)

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_gpe_block
 *
 * PARAMETERS:  gpe_device          - Handle to the parent GPE Block Device
 *              gpe_block_address   - Address and space_ID
 *              register_count      - Number of GPE register pairs in the block
 *              interrupt_number    - H/W interrupt for the block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create and Install a block of GPE registers. The GPEs are not
 *              enabled here.
 *
 ******************************************************************************/
acpi_status
acpi_install_gpe_block(acpi_handle gpe_device,
		       struct acpi_generic_address *gpe_block_address,
		       u32 register_count, u32 interrupt_number)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	struct acpi_gpe_block_info *gpe_block;

	ACPI_FUNCTION_TRACE(acpi_install_gpe_block);

	if ((!gpe_device) || (!gpe_block_address) || (!register_count)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	node = acpi_ns_validate_handle(gpe_device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Validate the parent device */

	if (node->type != ACPI_TYPE_DEVICE) {
		status = AE_TYPE;
		goto unlock_and_exit;
	}

	if (node->object) {
		status = AE_ALREADY_EXISTS;
		goto unlock_and_exit;
	}

	/*
	 * For user-installed GPE Block Devices, the gpe_block_base_number
	 * is always zero
	 */
	status = acpi_ev_create_gpe_block(node, gpe_block_address->address,
					  gpe_block_address->space_id,
					  register_count, 0, interrupt_number,
					  &gpe_block);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Install block in the device_object attached to the node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {

		/*
		 * No object, create a new one (Device nodes do not always have
		 * an attached object)
		 */
		obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_DEVICE);
		if (!obj_desc) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		status =
		    acpi_ns_attach_object(node, obj_desc, ACPI_TYPE_DEVICE);

		/* Remove local reference to the object */

		acpi_ut_remove_reference(obj_desc);

		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	/* Now install the GPE block in the device_object */

	obj_desc->device.gpe_block = gpe_block;

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_gpe_block)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_gpe_block
 *
 * PARAMETERS:  gpe_device          - Handle to the parent GPE Block Device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a previously installed block of GPE registers
 *
 ******************************************************************************/
acpi_status acpi_remove_gpe_block(acpi_handle gpe_device)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_remove_gpe_block);

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	node = acpi_ns_validate_handle(gpe_device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Validate the parent device */

	if (node->type != ACPI_TYPE_DEVICE) {
		status = AE_TYPE;
		goto unlock_and_exit;
	}

	/* Get the device_object attached to the node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc || !obj_desc->device.gpe_block) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	/* Delete the GPE block (but not the device_object) */

	status = acpi_ev_delete_gpe_block(obj_desc->device.gpe_block);
	if (ACPI_SUCCESS(status)) {
		obj_desc->device.gpe_block = NULL;
	}

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_gpe_block)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_gpe_device
 *
 * PARAMETERS:  index               - System GPE index (0-current_gpe_count)
 *              gpe_device          - Where the parent GPE Device is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain the GPE device associated with the input index. A NULL
 *              gpe device indicates that the gpe number is contained in one of
 *              the FADT-defined gpe blocks. Otherwise, the GPE block device.
 *
 ******************************************************************************/
acpi_status acpi_get_gpe_device(u32 index, acpi_handle *gpe_device)
{
	struct acpi_gpe_device_info info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_gpe_device);

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (index >= acpi_current_gpe_count) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Setup and walk the GPE list */

	info.index = index;
	info.status = AE_NOT_EXIST;
	info.gpe_device = NULL;
	info.next_block_base_index = 0;

	status = acpi_ev_walk_gpe_list(acpi_ev_get_gpe_device, &info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*gpe_device = ACPI_CAST_PTR(acpi_handle, info.gpe_device);
	return_ACPI_STATUS(info.status);
}

ACPI_EXPORT_SYMBOL(acpi_get_gpe_device)
#endif				/* !ACPI_REDUCED_HARDWARE */
