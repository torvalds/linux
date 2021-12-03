/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _CRYPTO_FIPS140_EVAL_TESTING_H
#define _CRYPTO_FIPS140_EVAL_TESTING_H

#include <linux/ioctl.h>

/*
 * This header defines the ioctls that are available on the fips140 character
 * device.  These ioctls expose some of the module's services to userspace so
 * that they can be tested by the FIPS certification lab; this is a required
 * part of getting a FIPS 140 certification.  These ioctls do not have any other
 * purpose, and they do not need to be present in production builds.
 */

/*
 * Call the fips140_is_approved_service() function.  The argument must be the
 * service name as a NUL-terminated string.  The return value will be 1 if
 * fips140_is_approved_service() returned true, or 0 if it returned false.
 */
#define FIPS140_IOCTL_IS_APPROVED_SERVICE	_IO('F', 0)

/*
 * Call the fips140_module_version() function.  The argument must be a pointer
 * to a buffer of size >= 256 chars.  The NUL-terminated string returned by
 * fips140_module_version() will be written to this buffer.
 */
#define FIPS140_IOCTL_MODULE_VERSION		_IOR('F', 1, char[256])

#endif /* _CRYPTO_FIPS140_EVAL_TESTING_H */
