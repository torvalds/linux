// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express device driver verbose errors
 * Copyright (c) 2022, Oracle and/or its affiliates
 */

#include "nvme.h"

static const char * const nvme_ops[] = {
	[nvme_cmd_flush] = "Flush",
	[nvme_cmd_write] = "Write",
	[nvme_cmd_read] = "Read",
	[nvme_cmd_write_uncor] = "Write Uncorrectable",
	[nvme_cmd_compare] = "Compare",
	[nvme_cmd_write_zeroes] = "Write Zeroes",
	[nvme_cmd_dsm] = "Dataset Management",
	[nvme_cmd_verify] = "Verify",
	[nvme_cmd_resv_register] = "Reservation Register",
	[nvme_cmd_resv_report] = "Reservation Report",
	[nvme_cmd_resv_acquire] = "Reservation Acquire",
	[nvme_cmd_resv_release] = "Reservation Release",
	[nvme_cmd_zone_mgmt_send] = "Zone Management Send",
	[nvme_cmd_zone_mgmt_recv] = "Zone Management Receive",
	[nvme_cmd_zone_append] = "Zone Append",
};

static const char * const nvme_admin_ops[] = {
	[nvme_admin_delete_sq] = "Delete SQ",
	[nvme_admin_create_sq] = "Create SQ",
	[nvme_admin_get_log_page] = "Get Log Page",
	[nvme_admin_delete_cq] = "Delete CQ",
	[nvme_admin_create_cq] = "Create CQ",
	[nvme_admin_identify] = "Identify",
	[nvme_admin_abort_cmd] = "Abort Command",
	[nvme_admin_set_features] = "Set Features",
	[nvme_admin_get_features] = "Get Features",
	[nvme_admin_async_event] = "Async Event",
	[nvme_admin_ns_mgmt] = "Namespace Management",
	[nvme_admin_activate_fw] = "Activate Firmware",
	[nvme_admin_download_fw] = "Download Firmware",
	[nvme_admin_dev_self_test] = "Device Self Test",
	[nvme_admin_ns_attach] = "Namespace Attach",
	[nvme_admin_keep_alive] = "Keep Alive",
	[nvme_admin_directive_send] = "Directive Send",
	[nvme_admin_directive_recv] = "Directive Receive",
	[nvme_admin_virtual_mgmt] = "Virtual Management",
	[nvme_admin_nvme_mi_send] = "NVMe Send MI",
	[nvme_admin_nvme_mi_recv] = "NVMe Receive MI",
	[nvme_admin_dbbuf] = "Doorbell Buffer Config",
	[nvme_admin_format_nvm] = "Format NVM",
	[nvme_admin_security_send] = "Security Send",
	[nvme_admin_security_recv] = "Security Receive",
	[nvme_admin_sanitize_nvm] = "Sanitize NVM",
	[nvme_admin_get_lba_status] = "Get LBA Status",
};

static const char * const nvme_fabrics_ops[] = {
	[nvme_fabrics_type_property_set] = "Property Set",
	[nvme_fabrics_type_property_get] = "Property Get",
	[nvme_fabrics_type_connect] = "Connect",
	[nvme_fabrics_type_auth_send] = "Authentication Send",
	[nvme_fabrics_type_auth_receive] = "Authentication Receive",
};

static const char * const nvme_statuses[] = {
	[NVME_SC_SUCCESS] = "Success",
	[NVME_SC_INVALID_OPCODE] = "Invalid Command Opcode",
	[NVME_SC_INVALID_FIELD] = "Invalid Field in Command",
	[NVME_SC_CMDID_CONFLICT] = "Command ID Conflict",
	[NVME_SC_DATA_XFER_ERROR] = "Data Transfer Error",
	[NVME_SC_POWER_LOSS] = "Commands Aborted due to Power Loss Notification",
	[NVME_SC_INTERNAL] = "Internal Error",
	[NVME_SC_ABORT_REQ] = "Command Abort Requested",
	[NVME_SC_ABORT_QUEUE] = "Command Aborted due to SQ Deletion",
	[NVME_SC_FUSED_FAIL] = "Command Aborted due to Failed Fused Command",
	[NVME_SC_FUSED_MISSING] = "Command Aborted due to Missing Fused Command",
	[NVME_SC_INVALID_NS] = "Invalid Namespace or Format",
	[NVME_SC_CMD_SEQ_ERROR] = "Command Sequence Error",
	[NVME_SC_SGL_INVALID_LAST] = "Invalid SGL Segment Descriptor",
	[NVME_SC_SGL_INVALID_COUNT] = "Invalid Number of SGL Descriptors",
	[NVME_SC_SGL_INVALID_DATA] = "Data SGL Length Invalid",
	[NVME_SC_SGL_INVALID_METADATA] = "Metadata SGL Length Invalid",
	[NVME_SC_SGL_INVALID_TYPE] = "SGL Descriptor Type Invalid",
	[NVME_SC_CMB_INVALID_USE] = "Invalid Use of Controller Memory Buffer",
	[NVME_SC_PRP_INVALID_OFFSET] = "PRP Offset Invalid",
	[NVME_SC_ATOMIC_WU_EXCEEDED] = "Atomic Write Unit Exceeded",
	[NVME_SC_OP_DENIED] = "Operation Denied",
	[NVME_SC_SGL_INVALID_OFFSET] = "SGL Offset Invalid",
	[NVME_SC_RESERVED] = "Reserved",
	[NVME_SC_HOST_ID_INCONSIST] = "Host Identifier Inconsistent Format",
	[NVME_SC_KA_TIMEOUT_EXPIRED] = "Keep Alive Timeout Expired",
	[NVME_SC_KA_TIMEOUT_INVALID] = "Keep Alive Timeout Invalid",
	[NVME_SC_ABORTED_PREEMPT_ABORT] = "Command Aborted due to Preempt and Abort",
	[NVME_SC_SANITIZE_FAILED] = "Sanitize Failed",
	[NVME_SC_SANITIZE_IN_PROGRESS] = "Sanitize In Progress",
	[NVME_SC_SGL_INVALID_GRANULARITY] = "SGL Data Block Granularity Invalid",
	[NVME_SC_CMD_NOT_SUP_CMB_QUEUE] = "Command Not Supported for Queue in CMB",
	[NVME_SC_NS_WRITE_PROTECTED] = "Namespace is Write Protected",
	[NVME_SC_CMD_INTERRUPTED] = "Command Interrupted",
	[NVME_SC_TRANSIENT_TR_ERR] = "Transient Transport Error",
	[NVME_SC_ADMIN_COMMAND_MEDIA_NOT_READY] = "Admin Command Media Not Ready",
	[NVME_SC_INVALID_IO_CMD_SET] = "Invalid IO Command Set",
	[NVME_SC_LBA_RANGE] = "LBA Out of Range",
	[NVME_SC_CAP_EXCEEDED] = "Capacity Exceeded",
	[NVME_SC_NS_NOT_READY] = "Namespace Not Ready",
	[NVME_SC_RESERVATION_CONFLICT] = "Reservation Conflict",
	[NVME_SC_FORMAT_IN_PROGRESS] = "Format In Progress",
	[NVME_SC_CQ_INVALID] = "Completion Queue Invalid",
	[NVME_SC_QID_INVALID] = "Invalid Queue Identifier",
	[NVME_SC_QUEUE_SIZE] = "Invalid Queue Size",
	[NVME_SC_ABORT_LIMIT] = "Abort Command Limit Exceeded",
	[NVME_SC_ABORT_MISSING] = "Reserved", /* XXX */
	[NVME_SC_ASYNC_LIMIT] = "Asynchronous Event Request Limit Exceeded",
	[NVME_SC_FIRMWARE_SLOT] = "Invalid Firmware Slot",
	[NVME_SC_FIRMWARE_IMAGE] = "Invalid Firmware Image",
	[NVME_SC_INVALID_VECTOR] = "Invalid Interrupt Vector",
	[NVME_SC_INVALID_LOG_PAGE] = "Invalid Log Page",
	[NVME_SC_INVALID_FORMAT] = "Invalid Format",
	[NVME_SC_FW_NEEDS_CONV_RESET] = "Firmware Activation Requires Conventional Reset",
	[NVME_SC_INVALID_QUEUE] = "Invalid Queue Deletion",
	[NVME_SC_FEATURE_NOT_SAVEABLE] = "Feature Identifier Not Saveable",
	[NVME_SC_FEATURE_NOT_CHANGEABLE] = "Feature Not Changeable",
	[NVME_SC_FEATURE_NOT_PER_NS] = "Feature Not Namespace Specific",
	[NVME_SC_FW_NEEDS_SUBSYS_RESET] = "Firmware Activation Requires NVM Subsystem Reset",
	[NVME_SC_FW_NEEDS_RESET] = "Firmware Activation Requires Reset",
	[NVME_SC_FW_NEEDS_MAX_TIME] = "Firmware Activation Requires Maximum Time Violation",
	[NVME_SC_FW_ACTIVATE_PROHIBITED] = "Firmware Activation Prohibited",
	[NVME_SC_OVERLAPPING_RANGE] = "Overlapping Range",
	[NVME_SC_NS_INSUFFICIENT_CAP] = "Namespace Insufficient Capacity",
	[NVME_SC_NS_ID_UNAVAILABLE] = "Namespace Identifier Unavailable",
	[NVME_SC_NS_ALREADY_ATTACHED] = "Namespace Already Attached",
	[NVME_SC_NS_IS_PRIVATE] = "Namespace Is Private",
	[NVME_SC_NS_NOT_ATTACHED] = "Namespace Not Attached",
	[NVME_SC_THIN_PROV_NOT_SUPP] = "Thin Provisioning Not Supported",
	[NVME_SC_CTRL_LIST_INVALID] = "Controller List Invalid",
	[NVME_SC_SELT_TEST_IN_PROGRESS] = "Device Self-test In Progress",
	[NVME_SC_BP_WRITE_PROHIBITED] = "Boot Partition Write Prohibited",
	[NVME_SC_CTRL_ID_INVALID] = "Invalid Controller Identifier",
	[NVME_SC_SEC_CTRL_STATE_INVALID] = "Invalid Secondary Controller State",
	[NVME_SC_CTRL_RES_NUM_INVALID] = "Invalid Number of Controller Resources",
	[NVME_SC_RES_ID_INVALID] = "Invalid Resource Identifier",
	[NVME_SC_PMR_SAN_PROHIBITED] = "Sanitize Prohibited",
	[NVME_SC_ANA_GROUP_ID_INVALID] = "ANA Group Identifier Invalid",
	[NVME_SC_ANA_ATTACH_FAILED] = "ANA Attach Failed",
	[NVME_SC_BAD_ATTRIBUTES] = "Conflicting Attributes",
	[NVME_SC_INVALID_PI] = "Invalid Protection Information",
	[NVME_SC_READ_ONLY] = "Attempted Write to Read Only Range",
	[NVME_SC_ONCS_NOT_SUPPORTED] = "ONCS Not Supported",
	[NVME_SC_ZONE_BOUNDARY_ERROR] = "Zoned Boundary Error",
	[NVME_SC_ZONE_FULL] = "Zone Is Full",
	[NVME_SC_ZONE_READ_ONLY] = "Zone Is Read Only",
	[NVME_SC_ZONE_OFFLINE] = "Zone Is Offline",
	[NVME_SC_ZONE_INVALID_WRITE] = "Zone Invalid Write",
	[NVME_SC_ZONE_TOO_MANY_ACTIVE] = "Too Many Active Zones",
	[NVME_SC_ZONE_TOO_MANY_OPEN] = "Too Many Open Zones",
	[NVME_SC_ZONE_INVALID_TRANSITION] = "Invalid Zone State Transition",
	[NVME_SC_WRITE_FAULT] = "Write Fault",
	[NVME_SC_READ_ERROR] = "Unrecovered Read Error",
	[NVME_SC_GUARD_CHECK] = "End-to-end Guard Check Error",
	[NVME_SC_APPTAG_CHECK] = "End-to-end Application Tag Check Error",
	[NVME_SC_REFTAG_CHECK] = "End-to-end Reference Tag Check Error",
	[NVME_SC_COMPARE_FAILED] = "Compare Failure",
	[NVME_SC_ACCESS_DENIED] = "Access Denied",
	[NVME_SC_UNWRITTEN_BLOCK] = "Deallocated or Unwritten Logical Block",
	[NVME_SC_INTERNAL_PATH_ERROR] = "Internal Pathing Error",
	[NVME_SC_ANA_PERSISTENT_LOSS] = "Asymmetric Access Persistent Loss",
	[NVME_SC_ANA_INACCESSIBLE] = "Asymmetric Access Inaccessible",
	[NVME_SC_ANA_TRANSITION] = "Asymmetric Access Transition",
	[NVME_SC_CTRL_PATH_ERROR] = "Controller Pathing Error",
	[NVME_SC_HOST_PATH_ERROR] = "Host Pathing Error",
	[NVME_SC_HOST_ABORTED_CMD] = "Host Aborted Command",
};

const unsigned char *nvme_get_error_status_str(u16 status)
{
	status &= 0x7ff;
	if (status < ARRAY_SIZE(nvme_statuses) && nvme_statuses[status])
		return nvme_statuses[status & 0x7ff];
	return "Unknown";
}

const unsigned char *nvme_get_opcode_str(u8 opcode)
{
	if (opcode < ARRAY_SIZE(nvme_ops) && nvme_ops[opcode])
		return nvme_ops[opcode];
	return "Unknown";
}
EXPORT_SYMBOL_GPL(nvme_get_opcode_str);

const unsigned char *nvme_get_admin_opcode_str(u8 opcode)
{
	if (opcode < ARRAY_SIZE(nvme_admin_ops) && nvme_admin_ops[opcode])
		return nvme_admin_ops[opcode];
	return "Unknown";
}
EXPORT_SYMBOL_GPL(nvme_get_admin_opcode_str);

const unsigned char *nvme_get_fabrics_opcode_str(u8 opcode) {
	if (opcode < ARRAY_SIZE(nvme_fabrics_ops) && nvme_fabrics_ops[opcode])
		return nvme_fabrics_ops[opcode];
	return "Unknown";
}
EXPORT_SYMBOL_GPL(nvme_get_fabrics_opcode_str);
