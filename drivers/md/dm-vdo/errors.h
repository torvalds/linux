/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_ERRORS_H
#define UDS_ERRORS_H

#include <linux/compiler.h>
#include <linux/types.h>

/* Custom error codes and error-related utilities for UDS */

/* Valid status codes for internal UDS functions. */
enum uds_status_codes {
	/* Successful return */
	UDS_SUCCESS = 0,

	/* Used as a base value for reporting internal errors */
	UDS_ERROR_CODE_BASE = 1024,
	/* Index overflow */
	UDS_OVERFLOW = UDS_ERROR_CODE_BASE + 0,
	/* Invalid argument passed to internal routine */
	UDS_INVALID_ARGUMENT = UDS_ERROR_CODE_BASE + 1,
	/* UDS data structures are in an invalid state */
	UDS_BAD_STATE = UDS_ERROR_CODE_BASE + 2,
	/* Attempt to enter the same name into an internal structure twice */
	UDS_DUPLICATE_NAME = UDS_ERROR_CODE_BASE + 3,
	/* An assertion failed */
	UDS_ASSERTION_FAILED = UDS_ERROR_CODE_BASE + 4,
	/* A request has been queued for later processing (not an error) */
	UDS_QUEUED = UDS_ERROR_CODE_BASE + 5,
	/* A problem has occurred with a buffer */
	UDS_BUFFER_ERROR = UDS_ERROR_CODE_BASE + 6,
	/* No directory was found where one was expected */
	UDS_NO_DIRECTORY = UDS_ERROR_CODE_BASE + 7,
	/* This error range has already been registered */
	UDS_ALREADY_REGISTERED = UDS_ERROR_CODE_BASE + 8,
	/* Attempt to read or write data outside the valid range */
	UDS_OUT_OF_RANGE = UDS_ERROR_CODE_BASE + 9,
	/* Could not load modules */
	UDS_EMODULE_LOAD = UDS_ERROR_CODE_BASE + 10,
	/* The index session is disabled */
	UDS_DISABLED = UDS_ERROR_CODE_BASE + 11,
	/* Unknown error */
	UDS_UNKNOWN_ERROR = UDS_ERROR_CODE_BASE + 12,
	/* The index configuration or volume format is no longer supported */
	UDS_UNSUPPORTED_VERSION = UDS_ERROR_CODE_BASE + 13,
	/* Some index structure is corrupt */
	UDS_CORRUPT_DATA = UDS_ERROR_CODE_BASE + 14,
	/* No index state found */
	UDS_NO_INDEX = UDS_ERROR_CODE_BASE + 15,
	/* Attempt to access incomplete index save data */
	UDS_INDEX_NOT_SAVED_CLEANLY = UDS_ERROR_CODE_BASE + 16,
	/* One more than the last UDS_INTERNAL error code */
	UDS_ERROR_CODE_LAST,
	/* One more than the last error this block will ever use */
	UDS_ERROR_CODE_BLOCK_END = UDS_ERROR_CODE_BASE + 440,
};

enum {
	UDS_MAX_ERROR_NAME_SIZE = 80,
	UDS_MAX_ERROR_MESSAGE_SIZE = 128,
};

struct error_info {
	const char *name;
	const char *message;
};

const char * __must_check uds_string_error(int errnum, char *buf, size_t buflen);

const char *uds_string_error_name(int errnum, char *buf, size_t buflen);

int uds_map_to_system_error(int error);

int uds_register_error_block(const char *block_name, int first_error,
			     int last_reserved_error, const struct error_info *infos,
			     size_t info_size);

#endif /* UDS_ERRORS_H */
