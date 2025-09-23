// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <linux/module.h>
#include <linux/net/intel/libie/adminq.h>

static const char * const libie_aq_str_arr[] = {
#define LIBIE_AQ_STR(x)					\
	[LIBIE_AQ_RC_##x]	= "LIBIE_AQ_RC_" #x
	LIBIE_AQ_STR(OK),
	LIBIE_AQ_STR(EPERM),
	LIBIE_AQ_STR(ENOENT),
	LIBIE_AQ_STR(ESRCH),
	LIBIE_AQ_STR(EIO),
	LIBIE_AQ_STR(EAGAIN),
	LIBIE_AQ_STR(ENOMEM),
	LIBIE_AQ_STR(EACCES),
	LIBIE_AQ_STR(EBUSY),
	LIBIE_AQ_STR(EEXIST),
	LIBIE_AQ_STR(EINVAL),
	LIBIE_AQ_STR(ENOSPC),
	LIBIE_AQ_STR(ENOSYS),
	LIBIE_AQ_STR(EMODE),
	LIBIE_AQ_STR(ENOSEC),
	LIBIE_AQ_STR(EBADSIG),
	LIBIE_AQ_STR(ESVN),
	LIBIE_AQ_STR(EBADMAN),
	LIBIE_AQ_STR(EBADBUF),
#undef LIBIE_AQ_STR
	"LIBIE_AQ_RC_UNKNOWN",
};

#define __LIBIE_AQ_STR_NUM (ARRAY_SIZE(libie_aq_str_arr) - 1)

/**
 * libie_aq_str - get error string based on aq error
 * @err: admin queue error type
 *
 * Return: error string for passed error code
 */
const char *libie_aq_str(enum libie_aq_err err)
{
	if (err >= ARRAY_SIZE(libie_aq_str_arr) ||
	    !libie_aq_str_arr[err])
		err = __LIBIE_AQ_STR_NUM;

	return libie_aq_str_arr[err];
}
EXPORT_SYMBOL_NS_GPL(libie_aq_str, "LIBIE_ADMINQ");

MODULE_DESCRIPTION("Intel(R) Ethernet common library - adminq helpers");
MODULE_LICENSE("GPL");
