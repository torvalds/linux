/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_STATUS_CODES_H
#define VDO_STATUS_CODES_H

#include "errors.h"

enum {
	UDS_ERRORS_BLOCK_SIZE = UDS_ERROR_CODE_BLOCK_END - UDS_ERROR_CODE_BASE,
	VDO_ERRORS_BLOCK_START = UDS_ERROR_CODE_BLOCK_END,
	VDO_ERRORS_BLOCK_END = VDO_ERRORS_BLOCK_START + UDS_ERRORS_BLOCK_SIZE,
};

/* VDO-specific status codes. */
enum vdo_status_codes {
	/* base of all VDO errors */
	VDO_STATUS_CODE_BASE = VDO_ERRORS_BLOCK_START,
	/* we haven't written this yet */
	VDO_NOT_IMPLEMENTED = VDO_STATUS_CODE_BASE,
	/* input out of range */
	VDO_OUT_OF_RANGE,
	/* an invalid reference count would result */
	VDO_REF_COUNT_INVALID,
	/* a free block could not be allocated */
	VDO_NO_SPACE,
	/* improper or missing configuration option */
	VDO_BAD_CONFIGURATION,
	/* prior operation still in progress */
	VDO_COMPONENT_BUSY,
	/* page contents incorrect or corrupt data */
	VDO_BAD_PAGE,
	/* unsupported version of some component */
	VDO_UNSUPPORTED_VERSION,
	/* component id mismatch in decoder */
	VDO_INCORRECT_COMPONENT,
	/* parameters have conflicting values */
	VDO_PARAMETER_MISMATCH,
	/* no partition exists with a given id */
	VDO_UNKNOWN_PARTITION,
	/* a partition already exists with a given id */
	VDO_PARTITION_EXISTS,
	/* physical block growth of too few blocks */
	VDO_INCREMENT_TOO_SMALL,
	/* incorrect checksum */
	VDO_CHECKSUM_MISMATCH,
	/* a lock is held incorrectly */
	VDO_LOCK_ERROR,
	/* the VDO is in read-only mode */
	VDO_READ_ONLY,
	/* the VDO is shutting down */
	VDO_SHUTTING_DOWN,
	/* the recovery journal has corrupt entries or corrupt metadata */
	VDO_CORRUPT_JOURNAL,
	/* exceeds maximum number of slabs supported */
	VDO_TOO_MANY_SLABS,
	/* a compressed block fragment is invalid */
	VDO_INVALID_FRAGMENT,
	/* action is unsupported while rebuilding */
	VDO_RETRY_AFTER_REBUILD,
	/* a block map entry is invalid */
	VDO_BAD_MAPPING,
	/* bio_add_page failed */
	VDO_BIO_CREATION_FAILED,
	/* bad magic number */
	VDO_BAD_MAGIC,
	/* bad nonce */
	VDO_BAD_NONCE,
	/* sequence number overflow */
	VDO_JOURNAL_OVERFLOW,
	/* the VDO is not in a state to perform an admin operation */
	VDO_INVALID_ADMIN_STATE,
	/* one more than last error code */
	VDO_STATUS_CODE_LAST,
	VDO_STATUS_CODE_BLOCK_END = VDO_ERRORS_BLOCK_END
};

extern const struct error_info vdo_status_list[];

int vdo_register_status_codes(void);

int vdo_status_to_errno(int error);

#endif /* VDO_STATUS_CODES_H */
