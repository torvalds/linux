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
	{ "VDO_BAD_CONFIGURATION", "Bad configuration option" },
	{ "VDO_COMPONENT_BUSY", "Prior operation still in progress" },
	{ "VDO_BAD_PAGE", "Corrupt or incorrect page" },
	{ "VDO_UNSUPPORTED_VERSION", "Unsupported component version" },
	{ "VDO_INCORRECT_COMPONENT", "Component id mismatch in decoder" },
	{ "VDO_PARAMETER_MISMATCH", "Parameters have conflicting values" },
	{ "VDO_UNKNOWN_PARTITION", "No partition exists with a given id" },
	{ "VDO_PARTITION_EXISTS", "A partition already exists with a given id" },
	{ "VDO_INCREMENT_TOO_SMALL", "Physical block growth of too few blocks" },
	{ "VDO_CHECKSUM_MISMATCH", "Incorrect checksum" },
	{ "VDO_LOCK_ERROR", "A lock is held incorrectly" },
	{ "VDO_READ_ONLY", "The device is in read-only mode" },
	{ "VDO_SHUTTING_DOWN", "The device is shutting down" },
	{ "VDO_CORRUPT_JOURNAL", "Recovery journal entries corrupted" },
	{ "VDO_TOO_MANY_SLABS", "Exceeds maximum number of slabs supported" },
	{ "VDO_INVALID_FRAGMENT", "Compressed block fragment is invalid" },
	{ "VDO_RETRY_AFTER_REBUILD", "Retry operation after rebuilding finishes" },
	{ "VDO_BAD_MAPPING", "Invalid page mapping" },
	{ "VDO_BIO_CREATION_FAILED", "Bio creation failed" },
	{ "VDO_BAD_MAGIC", "Bad magic number" },
	{ "VDO_BAD_NONCE", "Bad nonce" },
	{ "VDO_JOURNAL_OVERFLOW", "Journal sequence number overflow" },
	{ "VDO_INVALID_ADMIN_STATE", "Invalid operation for current state" },
};

/**
 * vdo_register_status_codes() - Register the VDO status codes.
 * Return: A success or error code.
 */
int vdo_register_status_codes(void)
{
	int result;

	BUILD_BUG_ON((VDO_STATUS_CODE_LAST - VDO_STATUS_CODE_BASE) !=
		     ARRAY_SIZE(vdo_status_list));

	result = uds_register_error_block("VDO Status", VDO_STATUS_CODE_BASE,
					  VDO_STATUS_CODE_BLOCK_END, vdo_status_list,
					  sizeof(vdo_status_list));
	return (result == UDS_SUCCESS) ? VDO_SUCCESS : result;
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
	char error_name[VDO_MAX_ERROR_NAME_SIZE];
	char error_message[VDO_MAX_ERROR_MESSAGE_SIZE];

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
		vdo_log_info("%s: mapping internal status code %d (%s: %s) to EIO",
			     __func__, error,
			     uds_string_error_name(error, error_name, sizeof(error_name)),
			     uds_string_error(error, error_message, sizeof(error_message)));
		return -EIO;
	}
}
