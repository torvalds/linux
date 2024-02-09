// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "status-codes.h"

#include "errors.h"
#include "logger.h"
#include "permassert.h"
#include "thread-utils.h"

const struct error_info vdo_status_list[] = {
	{ "VDO_NOT_IMPLEMENTED", "Not implemented" },
	{ "VDO_OUT_OF_RANGE", "Out of range" },
	{ "VDO_REF_COUNT_INVALID", "Reference count would become invalid" },
	{ "VDO_NO_SPACE", "Out of space" },
	{ "VDO_UNEXPECTED_EOF", "Unexpected EOF on block read" },
	{ "VDO_BAD_CONFIGURATION", "Bad configuration option" },
	{ "VDO_SOCKET_ERROR", "Socket error" },
	{ "VDO_BAD_ALIGNMENT", "Mis-aligned block reference" },
	{ "VDO_COMPONENT_BUSY", "Prior operation still in progress" },
	{ "VDO_BAD_PAGE", "Corrupt or incorrect page" },
	{ "VDO_UNSUPPORTED_VERSION", "Unsupported component version" },
	{ "VDO_INCORRECT_COMPONENT", "Component id mismatch in decoder" },
	{ "VDO_PARAMETER_MISMATCH", "Parameters have conflicting values" },
	{ "VDO_BLOCK_SIZE_TOO_SMALL", "The block size is too small" },
	{ "VDO_UNKNOWN_PARTITION", "No partition exists with a given id" },
	{ "VDO_PARTITION_EXISTS", "A partition already exists with a given id" },
	{ "VDO_NOT_READ_ONLY", "The device is not in read-only mode" },
	{ "VDO_INCREMENT_TOO_SMALL", "Physical block growth of too few blocks" },
	{ "VDO_CHECKSUM_MISMATCH", "Incorrect checksum" },
	{ "VDO_RECOVERY_JOURNAL_FULL", "The recovery journal is full" },
	{ "VDO_LOCK_ERROR", "A lock is held incorrectly" },
	{ "VDO_READ_ONLY", "The device is in read-only mode" },
	{ "VDO_SHUTTING_DOWN", "The device is shutting down" },
	{ "VDO_CORRUPT_JOURNAL", "Recovery journal entries corrupted" },
	{ "VDO_TOO_MANY_SLABS", "Exceeds maximum number of slabs supported" },
	{ "VDO_INVALID_FRAGMENT", "Compressed block fragment is invalid" },
	{ "VDO_RETRY_AFTER_REBUILD", "Retry operation after rebuilding finishes" },
	{ "VDO_UNKNOWN_COMMAND", "The extended command is not known" },
	{ "VDO_COMMAND_ERROR", "Bad extended command parameters" },
	{ "VDO_CANNOT_DETERMINE_SIZE", "Cannot determine config sizes to fit" },
	{ "VDO_BAD_MAPPING", "Invalid page mapping" },
	{ "VDO_READ_CACHE_BUSY", "Read cache has no free slots" },
	{ "VDO_BIO_CREATION_FAILED", "Bio creation failed" },
	{ "VDO_BAD_MAGIC", "Bad magic number" },
	{ "VDO_BAD_NONCE", "Bad nonce" },
	{ "VDO_JOURNAL_OVERFLOW", "Journal sequence number overflow" },
	{ "VDO_INVALID_ADMIN_STATE", "Invalid operation for current state" },
	{ "VDO_CANT_ADD_SYSFS_NODE", "Failed to add sysfs node" },
};

static atomic_t vdo_status_codes_registered = ATOMIC_INIT(0);
static int status_code_registration_result;

static void do_status_code_registration(void)
{
	int result;

	BUILD_BUG_ON((VDO_STATUS_CODE_LAST - VDO_STATUS_CODE_BASE) !=
		     ARRAY_SIZE(vdo_status_list));

	result = uds_register_error_block("VDO Status", VDO_STATUS_CODE_BASE,
					  VDO_STATUS_CODE_BLOCK_END, vdo_status_list,
					  sizeof(vdo_status_list));
	/*
	 * The following test handles cases where libvdo is statically linked against both the test
	 * modules and the test driver (because multiple instances of this module call their own
	 * copy of this function once each, resulting in multiple calls to register_error_block
	 * which is shared in libuds).
	 */
	if (result == UDS_DUPLICATE_NAME)
		result = UDS_SUCCESS;

	status_code_registration_result = (result == UDS_SUCCESS) ? VDO_SUCCESS : result;
}

/**
 * vdo_register_status_codes() - Register the VDO status codes if needed.
 * Return: A success or error code.
 */
int vdo_register_status_codes(void)
{
	uds_perform_once(&vdo_status_codes_registered, do_status_code_registration);
	return status_code_registration_result;
}

/**
 * vdo_status_to_errno() - Given an error code, return a value we can return to the OS.
 * @error: The error code to convert.
 *
 * The input error code may be a system-generated value (such as -EIO), an errno macro used in our
 * code (such as EIO), or a UDS or VDO status code; the result must be something the rest of the OS
 * can consume (negative errno values such as -EIO, in the case of the kernel).
 *
 * Return: A system error code value.
 */
int vdo_status_to_errno(int error)
{
	char error_name[UDS_MAX_ERROR_NAME_SIZE];
	char error_message[UDS_MAX_ERROR_MESSAGE_SIZE];

	/* 0 is success, negative a system error code */
	if (likely(error <= 0))
		return error;
	if (error < 1024)
		return -error;

	/* VDO or UDS error */
	switch (error) {
	case VDO_NO_SPACE:
		return -ENOSPC;
	case VDO_READ_ONLY:
		return -EIO;
	default:
		uds_log_info("%s: mapping internal status code %d (%s: %s) to EIO",
			     __func__, error,
			     uds_string_error_name(error, error_name, sizeof(error_name)),
			     uds_string_error(error, error_message, sizeof(error_message)));
		return -EIO;
	}
}
