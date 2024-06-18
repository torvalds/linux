// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: nsinit - namespace initialization
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsinit")

/* Local prototypes */
static acpi_status
acpi_ns_init_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value);

static acpi_status
acpi_ns_init_one_device(acpi_handle obj_handle,
			u32 nesting_level, void *context, void **return_value);

static acpi_status
acpi_ns_find_ini_methods(acpi_handle obj_handle,
			 u32 nesting_level, void *context, void **return_value);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_initialize_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the entire namespace and perform any necessary
 *              initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status acpi_ns_initialize_objects(void)
{
	acpi_status status;
	struct acpi_init_walk_info info;

	ACPI_FUNCTION_TRACE(ns_initialize_objects);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "[Init] Completing Initialization of ACPI Objects\n"));
	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
			      "Final data object initialization: "));

	/* Clear the info block */

	memset(&info, 0, sizeof(struct acpi_init_walk_info));

	/* Walk entire namespace from the supplied root */

	/*
	 * TBD: will become ACPI_TYPE_PACKAGE as this type object
	 * is now the only one that supports deferred initialization
	 * (forward references).
	 */
	status = acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX, acpi_ns_init_one_object,
				     NULL, &info, NULL);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "During WalkNamespace"));
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
			      "Namespace contains %u (0x%X) objects\n",
			      info.object_count, info.object_count));

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "%u Control Methods found\n%u Op Regions found\n",
			  info.method_count, info.op_region_count));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_initialize_devices
 *
 * PARAMETERS:  None
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Walk the entire namespace and initialize all ACPI devices.
 *              This means running _INI on all present devices.
 *
 *              Note: We install PCI config space handler on region access,
 *              not here.
 *
 ******************************************************************************/

acpi_status acpi_ns_initialize_devices(u32 flags)
{
	acpi_status status = AE_OK;
	struct acpi_device_walk_info info;
	acpi_handle handle;

	ACPI_FUNCTION_TRACE(ns_initialize_devices);

	if (!(flags & ACPI_NO_DEVICE_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Initializing ACPI Devices\n"));

		/* Init counters */

		info.device_count = 0;
		info.num_STA = 0;
		info.num_INI = 0;

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
				      "Initializing Device/Processor/Thermal objects "
				      "and executing _INI/_STA methods:\n"));

		/* Tree analysis: find all subtrees that contain _INI methods */

		status = acpi_ns_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
						ACPI_UINT32_MAX, FALSE,
						acpi_ns_find_ini_methods, NULL,
						&info, NULL);
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		/* Allocate the evaluation information block */

		info.evaluate_info =
		    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
		if (!info.evaluate_info) {
			status = AE_NO_MEMORY;
			goto error_exit;
		}

		/*
		 * Execute the "global" _INI method that may appear at the root.
		 * This support is provided for Windows compatibility (Vista+) and
		 * is not part of the ACPI specification.
		 */
		info.evaluate_info->prefix_node = acpi_gbl_root_node;
		info.evaluate_info->relative_pathname = METHOD_NAME__INI;
		info.evaluate_info->parameters = NULL;
		info.evaluate_info->flags = ACPI_IGNORE_RETURN_VALUE;

		status = acpi_ns_evaluate(info.evaluate_info);
		if (ACPI_SUCCESS(status)) {
			info.num_INI++;
		}

		/*
		 * Execute \_SB._INI.
		 * There appears to be a strict order requirement for \_SB._INI,
		 * which should be evaluated before any _REG evaluations.
		 */
		status = acpi_get_handle(NULL, "\\_SB", &handle);
		if (ACPI_SUCCESS(status)) {
			memset(info.evaluate_info, 0,
			       sizeof(struct acpi_evaluate_info));
			info.evaluate_info->prefix_node = handle;
			info.evaluate_info->relative_pathname =
			    METHOD_NAME__INI;
			info.evaluate_info->parameters = NULL;
			info.evaluate_info->flags = ACPI_IGNORE_RETURN_VALUE;

			status = acpi_ns_evaluate(info.evaluate_info);
			if (ACPI_SUCCESS(status)) {
				info.num_INI++;
			}
		}
	}

	/*
	 * Run all _REG methods
	 *
	 * Note: Any objects accessed by the _REG methods will be automatically
	 * initialized, even if they contain executable AML (see the call to
	 * acpi_ns_initialize_objects below).
	 *
	 * Note: According to the ACPI specification, we actually needn't execute
	 * _REG for system_memory/system_io operation regions, but for PCI_Config
	 * operation regions, it is required to evaluate _REG for those on a PCI
	 * root bus that doesn't contain _BBN object. So this code is kept here
	 * in order not to break things.
	 */
	if (!(flags & ACPI_NO_ADDRESS_SPACE_INIT)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "[Init] Executing _REG OpRegion methods\n"));

		status = acpi_ev_initialize_op_regions();
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}
	}

	if (!(flags & ACPI_NO_DEVICE_INIT)) {

		/* Walk namespace to execute all _INIs on present devices */

		status = acpi_ns_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
						ACPI_UINT32_MAX, FALSE,
						acpi_ns_init_one_device, NULL,
						&info, NULL);

		/*
		 * Any _OSI requests should be completed by now. If the BIOS has
		 * requested any Windows OSI strings, we will always truncate
		 * I/O addresses to 16 bits -- for Windows compatibility.
		 */
		if (acpi_gbl_osi_data >= ACPI_OSI_WIN_2000) {
			acpi_gbl_truncate_io_addresses = TRUE;
		}

		ACPI_FREE(info.evaluate_info);
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
				      "    Executed %u _INI methods requiring %u _STA executions "
				      "(examined %u objects)\n",
				      info.num_INI, info.num_STA,
				      info.device_count));
	}

	return_ACPI_STATUS(status);

error_exit:
	ACPI_EXCEPTION((AE_INFO, status, "During device initialization"));
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_package
 *
 * PARAMETERS:  obj_handle      - Node
 *              level           - Current nesting level
 *              context         - Not used
 *              return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from acpi_walk_namespace. Invoked for every package
 *              within the namespace. Used during dynamic load of an SSDT.
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_package(acpi_handle obj_handle,
			 u32 level, void *context, void **return_value)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		return (AE_OK);
	}

	/* Exit if package is already initialized */

	if (obj_desc->package.flags & AOPOBJ_DATA_VALID) {
		return (AE_OK);
	}

	status = acpi_ds_get_package_arguments(obj_desc);
	if (ACPI_FAILURE(status)) {
		return (AE_OK);
	}

	status =
	    acpi_ut_walk_package_tree(obj_desc, NULL,
				      acpi_ds_init_package_element, NULL);
	if (ACPI_FAILURE(status)) {
		return (AE_OK);
	}

	obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_object
 *
 * PARAMETERS:  obj_handle      - Node
 *              level           - Current nesting level
 *              context         - Points to a init info struct
 *              return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from acpi_walk_namespace. Invoked for every object
 *              within the namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Op Regions
 *
 ******************************************************************************/

static acpi_status
acpi_ns_init_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value)
{
	acpi_object_type type;
	acpi_status status = AE_OK;
	struct acpi_init_walk_info *info =
	    (struct acpi_init_walk_info *)context;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_NAME(ns_init_one_object);

	info->object_count++;

	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type(obj_handle);
	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		return (AE_OK);
	}

	/* Increment counters for object types we are looking for */

	switch (type) {
	case ACPI_TYPE_REGION:

		info->op_region_count++;
		break;

	case ACPI_TYPE_BUFFER_FIELD:

		info->field_count++;
		break;

	case ACPI_TYPE_LOCAL_BANK_FIELD:

		info->field_count++;
		break;

	case ACPI_TYPE_BUFFER:

		info->buffer_count++;
		break;

	case ACPI_TYPE_PACKAGE:

		info->package_count++;
		break;

	default:

		/* No init required, just exit now */

		return (AE_OK);
	}

	/* If the object is already initialized, nothing else to do */

	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return (AE_OK);
	}

	/* Must lock the interpreter before executing AML code */

	acpi_ex_enter_interpreter();

	/*
	 * Only initialization of Package objects can be deferred, in order
	 * to support forward references.
	 */
	switch (type) {
	case ACPI_TYPE_LOCAL_BANK_FIELD:

		/* TBD: bank_fields do not require deferred init, remove this code */

		info->field_init++;
		status = acpi_ds_get_bank_field_arguments(obj_desc);
		break;

	case ACPI_TYPE_PACKAGE:

		/* Complete the initialization/resolution of the package object */

		info->package_init++;
		status =
		    acpi_ns_init_one_package(obj_handle, level, NULL, NULL);
		break;

	default:

		/* No other types should get here */

		status = AE_TYPE;
		ACPI_EXCEPTION((AE_INFO, status,
				"Opcode is not deferred [%4.4s] (%s)",
				acpi_ut_get_node_name(node),
				acpi_ut_get_type_name(type)));
		break;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not execute arguments for [%4.4s] (%s)",
				acpi_ut_get_node_name(node),
				acpi_ut_get_type_name(type)));
	}

	/*
	 * We ignore errors from above, and always return OK, since we don't want
	 * to abort the walk on any single error.
	 */
	acpi_ex_exit_interpreter();
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_find_ini_methods
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Called during namespace walk. Finds objects named _INI under
 *              device/processor/thermal objects, and marks the entire subtree
 *              with a SUBTREE_HAS_INI flag. This flag is used during the
 *              subsequent device initialization walk to avoid entire subtrees
 *              that do not contain an _INI.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_find_ini_methods(acpi_handle obj_handle,
			 u32 nesting_level, void *context, void **return_value)
{
	struct acpi_device_walk_info *info =
	    ACPI_CAST_PTR(struct acpi_device_walk_info, context);
	struct acpi_namespace_node *node;
	struct acpi_namespace_node *parent_node;

	/* Keep count of device/processor/thermal objects */

	node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_handle);
	if ((node->type == ACPI_TYPE_DEVICE) ||
	    (node->type == ACPI_TYPE_PROCESSOR) ||
	    (node->type == ACPI_TYPE_THERMAL)) {
		info->device_count++;
		return (AE_OK);
	}

	/* We are only looking for methods named _INI */

	if (!ACPI_COMPARE_NAMESEG(node->name.ascii, METHOD_NAME__INI)) {
		return (AE_OK);
	}

	/*
	 * The only _INI methods that we care about are those that are
	 * present under Device, Processor, and Thermal objects.
	 */
	parent_node = node->parent;
	switch (parent_node->type) {
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_THERMAL:

		/* Mark parent and bubble up the INI present flag to the root */

		while (parent_node) {
			parent_node->flags |= ANOBJ_SUBTREE_HAS_INI;
			parent_node = parent_node->parent;
		}
		break;

	default:

		break;
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_device
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: This is called once per device soon after ACPI is enabled
 *              to initialize each device. It determines if the device is
 *              present, and if so, calls _INI.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_init_one_device(acpi_handle obj_handle,
			u32 nesting_level, void *context, void **return_value)
{
	struct acpi_device_walk_info *walk_info =
	    ACPI_CAST_PTR(struct acpi_device_walk_info, context);
	struct acpi_evaluate_info *info = walk_info->evaluate_info;
	u32 flags;
	acpi_status status;
	struct acpi_namespace_node *device_node;

	ACPI_FUNCTION_TRACE(ns_init_one_device);

	/* We are interested in Devices, Processors and thermal_zones only */

	device_node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_handle);
	if ((device_node->type != ACPI_TYPE_DEVICE) &&
	    (device_node->type != ACPI_TYPE_PROCESSOR) &&
	    (device_node->type != ACPI_TYPE_THERMAL)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Because of an earlier namespace analysis, all subtrees that contain an
	 * _INI method are tagged.
	 *
	 * If this device subtree does not contain any _INI methods, we
	 * can exit now and stop traversing this entire subtree.
	 */
	if (!(device_node->flags & ANOBJ_SUBTREE_HAS_INI)) {
		return_ACPI_STATUS(AE_CTRL_DEPTH);
	}

	/*
	 * Run _STA to determine if this device is present and functioning. We
	 * must know this information for two important reasons (from ACPI spec):
	 *
	 * 1) We can only run _INI if the device is present.
	 * 2) We must abort the device tree walk on this subtree if the device is
	 *    not present and is not functional (we will not examine the children)
	 *
	 * The _STA method is not required to be present under the device, we
	 * assume the device is present if _STA does not exist.
	 */
	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_METHOD, device_node, METHOD_NAME__STA));

	status = acpi_ut_execute_STA(device_node, &flags);
	if (ACPI_FAILURE(status)) {

		/* Ignore error and move on to next device */

		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Flags == -1 means that _STA was not found. In this case, we assume that
	 * the device is both present and functional.
	 *
	 * From the ACPI spec, description of _STA:
	 *
	 * "If a device object (including the processor object) does not have an
	 * _STA object, then OSPM assumes that all of the above bits are set (in
	 * other words, the device is present, ..., and functioning)"
	 */
	if (flags != ACPI_UINT32_MAX) {
		walk_info->num_STA++;
	}

	/*
	 * Examine the PRESENT and FUNCTIONING status bits
	 *
	 * Note: ACPI spec does not seem to specify behavior for the present but
	 * not functioning case, so we assume functioning if present.
	 */
	if (!(flags & ACPI_STA_DEVICE_PRESENT)) {

		/* Device is not present, we must examine the Functioning bit */

		if (flags & ACPI_STA_DEVICE_FUNCTIONING) {
			/*
			 * Device is not present but is "functioning". In this case,
			 * we will not run _INI, but we continue to examine the children
			 * of this device.
			 *
			 * From the ACPI spec, description of _STA: (note - no mention
			 * of whether to run _INI or not on the device in question)
			 *
			 * "_STA may return bit 0 clear (not present) with bit 3 set
			 * (device is functional). This case is used to indicate a valid
			 * device for which no device driver should be loaded (for example,
			 * a bridge device.) Children of this device may be present and
			 * valid. OSPM should continue enumeration below a device whose
			 * _STA returns this bit combination"
			 */
			return_ACPI_STATUS(AE_OK);
		} else {
			/*
			 * Device is not present and is not functioning. We must abort the
			 * walk of this subtree immediately -- don't look at the children
			 * of such a device.
			 *
			 * From the ACPI spec, description of _INI:
			 *
			 * "If the _STA method indicates that the device is not present,
			 * OSPM will not run the _INI and will not examine the children
			 * of the device for _INI methods"
			 */
			return_ACPI_STATUS(AE_CTRL_DEPTH);
		}
	}

	/*
	 * The device is present or is assumed present if no _STA exists.
	 * Run the _INI if it exists (not required to exist)
	 *
	 * Note: We know there is an _INI within this subtree, but it may not be
	 * under this particular device, it may be lower in the branch.
	 */
	if (!ACPI_COMPARE_NAMESEG(device_node->name.ascii, "_SB_") ||
	    device_node->parent != acpi_gbl_root_node) {
		ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
				(ACPI_TYPE_METHOD, device_node,
				 METHOD_NAME__INI));

		memset(info, 0, sizeof(struct acpi_evaluate_info));
		info->prefix_node = device_node;
		info->relative_pathname = METHOD_NAME__INI;
		info->parameters = NULL;
		info->flags = ACPI_IGNORE_RETURN_VALUE;

		status = acpi_ns_evaluate(info);
		if (ACPI_SUCCESS(status)) {
			walk_info->num_INI++;
		}
#ifdef ACPI_DEBUG_OUTPUT
		else if (status != AE_NOT_FOUND) {

			/* Ignore error and move on to next device */

			char *scope_name =
			    acpi_ns_get_normalized_pathname(device_node, TRUE);

			ACPI_EXCEPTION((AE_INFO, status,
					"during %s._INI execution",
					scope_name));
			ACPI_FREE(scope_name);
		}
#endif
	}

	/* Ignore errors from above */

	status = AE_OK;

	/*
	 * The _INI method has been run if present; call the Global Initialization
	 * Handler for this device.
	 */
	if (acpi_gbl_init_handler) {
		status =
		    acpi_gbl_init_handler(device_node, ACPI_INIT_DEVICE_INI);
	}

	return_ACPI_STATUS(status);
}
