// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "errors.h"

#include <linux/compiler.h>
#include <linux/errno.h>

#include "logger.h"
#include "permassert.h"
#include "string-utils.h"

static const struct error_info successful = { "UDS_SUCCESS", "Success" };

static const char *const message_table[] = {
	[EPERM] = "Operation not permitted",
	[ENOENT] = "No such file or directory",
	[ESRCH] = "No such process",
	[EINTR] = "Interrupted system call",
	[EIO] = "Input/output error",
	[ENXIO] = "No such device or address",
	[E2BIG] = "Argument list too long",
	[ENOEXEC] = "Exec format error",
	[EBADF] = "Bad file descriptor",
	[ECHILD] = "No child processes",
	[EAGAIN] = "Resource temporarily unavailable",
	[ENOMEM] = "Cannot allocate memory",
	[EACCES] = "Permission denied",
	[EFAULT] = "Bad address",
	[ENOTBLK] = "Block device required",
	[EBUSY] = "Device or resource busy",
	[EEXIST] = "File exists",
	[EXDEV] = "Invalid cross-device link",
	[ENODEV] = "No such device",
	[ENOTDIR] = "Not a directory",
	[EISDIR] = "Is a directory",
	[EINVAL] = "Invalid argument",
	[ENFILE] = "Too many open files in system",
	[EMFILE] = "Too many open files",
	[ENOTTY] = "Inappropriate ioctl for device",
	[ETXTBSY] = "Text file busy",
	[EFBIG] = "File too large",
	[ENOSPC] = "No space left on device",
	[ESPIPE] = "Illegal seek",
	[EROFS] = "Read-only file system",
	[EMLINK] = "Too many links",
	[EPIPE] = "Broken pipe",
	[EDOM] = "Numerical argument out of domain",
	[ERANGE] = "Numerical result out of range"
};

static const struct error_info error_list[] = {
	{ "UDS_OVERFLOW", "Index overflow" },
	{ "UDS_INVALID_ARGUMENT", "Invalid argument passed to internal routine" },
	{ "UDS_BAD_STATE", "UDS data structures are in an invalid state" },
	{ "UDS_DUPLICATE_NAME", "Attempt to enter the same name into a delta index twice" },
	{ "UDS_ASSERTION_FAILED", "Assertion failed" },
	{ "UDS_QUEUED", "Request queued" },
	{ "UDS_ALREADY_REGISTERED", "Error range already registered" },
	{ "UDS_OUT_OF_RANGE", "Cannot access data outside specified limits" },
	{ "UDS_DISABLED", "UDS library context is disabled" },
	{ "UDS_UNSUPPORTED_VERSION", "Unsupported version" },
	{ "UDS_CORRUPT_DATA", "Some index structure is corrupt" },
	{ "UDS_NO_INDEX", "No index found" },
	{ "UDS_INDEX_NOT_SAVED_CLEANLY", "Index not saved cleanly" },
};

struct error_block {
	const char *name;
	int base;
	int last;
	int max;
	const struct error_info *infos;
};

#define MAX_ERROR_BLOCKS 6

static struct {
	int allocated;
	int count;
	struct error_block blocks[MAX_ERROR_BLOCKS];
} registered_errors = {
	.allocated = MAX_ERROR_BLOCKS,
	.count = 1,
	.blocks = { {
			.name = "UDS Error",
			.base = UDS_ERROR_CODE_BASE,
			.last = UDS_ERROR_CODE_LAST,
			.max = UDS_ERROR_CODE_BLOCK_END,
			.infos = error_list,
		  } },
};

/* Get the error info for an error number. Also returns the name of the error block, if known. */
static const char *get_error_info(int errnum, const struct error_info **info_ptr)
{
	struct error_block *block;

	if (errnum == UDS_SUCCESS) {
		*info_ptr = &successful;
		return NULL;
	}

	for (block = registered_errors.blocks;
	     block < registered_errors.blocks + registered_errors.count;
	     block++) {
		if ((errnum >= block->base) && (errnum < block->last)) {
			*info_ptr = block->infos + (errnum - block->base);
			return block->name;
		} else if ((errnum >= block->last) && (errnum < block->max)) {
			*info_ptr = NULL;
			return block->name;
		}
	}

	return NULL;
}

/* Return a string describing a system error message. */
static const char *system_string_error(int errnum, char *buf, size_t buflen)
{
	size_t len;
	const char *error_string = NULL;

	if ((errnum > 0) && (errnum < ARRAY_SIZE(message_table)))
		error_string = message_table[errnum];

	len = ((error_string == NULL) ?
		 snprintf(buf, buflen, "Unknown error %d", errnum) :
		 snprintf(buf, buflen, "%s", error_string));
	if (len < buflen)
		return buf;

	buf[0] = '\0';
	return "System error";
}

/* Convert an error code to a descriptive string. */
const char *uds_string_error(int errnum, char *buf, size_t buflen)
{
	char *buffer = buf;
	char *buf_end = buf + buflen;
	const struct error_info *info = NULL;
	const char *block_name;

	if (buf == NULL)
		return NULL;

	if (errnum < 0)
		errnum = -errnum;

	block_name = get_error_info(errnum, &info);
	if (block_name != NULL) {
		if (info != NULL) {
			buffer = vdo_append_to_buffer(buffer, buf_end, "%s: %s",
						      block_name, info->message);
		} else {
			buffer = vdo_append_to_buffer(buffer, buf_end, "Unknown %s %d",
						      block_name, errnum);
		}
	} else if (info != NULL) {
		buffer = vdo_append_to_buffer(buffer, buf_end, "%s", info->message);
	} else {
		const char *tmp = system_string_error(errnum, buffer, buf_end - buffer);

		if (tmp != buffer)
			buffer = vdo_append_to_buffer(buffer, buf_end, "%s", tmp);
		else
			buffer += strlen(tmp);
	}

	return buf;
}

/* Convert an error code to its name. */
const char *uds_string_error_name(int errnum, char *buf, size_t buflen)
{
	char *buffer = buf;
	char *buf_end = buf + buflen;
	const struct error_info *info = NULL;
	const char *block_name;

	if (errnum < 0)
		errnum = -errnum;

	block_name = get_error_info(errnum, &info);
	if (block_name != NULL) {
		if (info != NULL) {
			buffer = vdo_append_to_buffer(buffer, buf_end, "%s", info->name);
		} else {
			buffer = vdo_append_to_buffer(buffer, buf_end, "%s %d",
						      block_name, errnum);
		}
	} else if (info != NULL) {
		buffer = vdo_append_to_buffer(buffer, buf_end, "%s", info->name);
	} else {
		const char *tmp;

		tmp = system_string_error(errnum, buffer, buf_end - buffer);
		if (tmp != buffer)
			buffer = vdo_append_to_buffer(buffer, buf_end, "%s", tmp);
		else
			buffer += strlen(tmp);
	}

	return buf;
}

/*
 * Translate an error code into a value acceptable to the kernel. The input error code may be a
 * system-generated value (such as -EIO), or an internal UDS status code. The result will be a
 * negative errno value.
 */
int uds_status_to_errno(int error)
{
	char error_name[VDO_MAX_ERROR_NAME_SIZE];
	char error_message[VDO_MAX_ERROR_MESSAGE_SIZE];

	/* 0 is success, and negative values are already system error codes. */
	if (likely(error <= 0))
		return error;

	if (error < 1024) {
		/* This is probably an errno from userspace. */
		return -error;
	}

	/* Internal UDS errors */
	switch (error) {
	case UDS_NO_INDEX:
	case UDS_CORRUPT_DATA:
		/* The index doesn't exist or can't be recovered. */
		return -ENOENT;

	case UDS_INDEX_NOT_SAVED_CLEANLY:
	case UDS_UNSUPPORTED_VERSION:
		/*
		 * The index exists, but can't be loaded. Tell the client it exists so they don't
		 * destroy it inadvertently.
		 */
		return -EEXIST;

	case UDS_DISABLED:
		/* The session is unusable; only returned by requests. */
		return -EIO;

	default:
		/* Translate an unexpected error into something generic. */
		vdo_log_info("%s: mapping status code %d (%s: %s) to -EIO",
			     __func__, error,
			     uds_string_error_name(error, error_name,
						   sizeof(error_name)),
			     uds_string_error(error, error_message,
					      sizeof(error_message)));
		return -EIO;
	}
}

/*
 * Register a block of error codes.
 *
 * @block_name: the name of the block of error codes
 * @first_error: the first error code in the block
 * @next_free_error: one past the highest possible error in the block
 * @infos: a pointer to the error info array for the block
 * @info_size: the size of the error info array
 */
int uds_register_error_block(const char *block_name, int first_error,
			     int next_free_error, const struct error_info *infos,
			     size_t info_size)
{
	int result;
	struct error_block *block;
	struct error_block new_block = {
		.name = block_name,
		.base = first_error,
		.last = first_error + (info_size / sizeof(struct error_info)),
		.max = next_free_error,
		.infos = infos,
	};

	result = VDO_ASSERT(first_error < next_free_error,
			    "well-defined error block range");
	if (result != VDO_SUCCESS)
		return result;

	if (registered_errors.count == registered_errors.allocated) {
		/* This should never happen. */
		return UDS_OVERFLOW;
	}

	for (block = registered_errors.blocks;
	     block < registered_errors.blocks + registered_errors.count;
	     block++) {
		if (strcmp(block_name, block->name) == 0)
			return UDS_DUPLICATE_NAME;

		/* Ensure error ranges do not overlap. */
		if ((first_error < block->max) && (next_free_error > block->base))
			return UDS_ALREADY_REGISTERED;
	}

	registered_errors.blocks[registered_errors.count++] = new_block;
	return UDS_SUCCESS;
}
