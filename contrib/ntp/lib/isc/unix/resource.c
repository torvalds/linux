/*
 * Copyright (C) 2004, 2007-2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: resource.c,v 1.23 2009/02/13 23:48:14 tbox Exp $ */

#include <config.h>

#include <sys/types.h>
#include <sys/time.h>	/* Required on some systems for <sys/resource.h>. */
#include <sys/resource.h>

#include <isc/platform.h>
#include <isc/resource.h>
#include <isc/result.h>
#include <isc/util.h>

#ifdef __linux__
#include <linux/fs.h>	/* To get the large NR_OPEN. */
#endif

#if defined(__hpux) && defined(HAVE_SYS_DYNTUNE_H)
#include <sys/dyntune.h>
#endif

#include "errno2result.h"

static isc_result_t
resource2rlim(isc_resource_t resource, int *rlim_resource) {
	isc_result_t result = ISC_R_SUCCESS;

	switch (resource) {
	case isc_resource_coresize:
		*rlim_resource = RLIMIT_CORE;
		break;
	case isc_resource_cputime:
		*rlim_resource = RLIMIT_CPU;
		break;
	case isc_resource_datasize:
		*rlim_resource = RLIMIT_DATA;
		break;
	case isc_resource_filesize:
		*rlim_resource = RLIMIT_FSIZE;
		break;
	case isc_resource_lockedmemory:
#ifdef RLIMIT_MEMLOCK
		*rlim_resource = RLIMIT_MEMLOCK;
#else
		result = ISC_R_NOTIMPLEMENTED;
#endif
		break;
	case isc_resource_openfiles:
#ifdef RLIMIT_NOFILE
		*rlim_resource = RLIMIT_NOFILE;
#else
		result = ISC_R_NOTIMPLEMENTED;
#endif
		break;
	case isc_resource_processes:
#ifdef RLIMIT_NPROC
		*rlim_resource = RLIMIT_NPROC;
#else
		result = ISC_R_NOTIMPLEMENTED;
#endif
		break;
	case isc_resource_residentsize:
#ifdef RLIMIT_RSS
		*rlim_resource = RLIMIT_RSS;
#else
		result = ISC_R_NOTIMPLEMENTED;
#endif
		break;
	case isc_resource_stacksize:
		*rlim_resource = RLIMIT_STACK;
		break;
	default:
		/*
		 * This test is not very robust if isc_resource_t
		 * changes, but generates a clear assertion message.
		 */
		REQUIRE(resource >= isc_resource_coresize &&
			resource <= isc_resource_stacksize);

		result = ISC_R_RANGE;
		break;
	}

	return (result);
}

isc_result_t
isc_resource_setlimit(isc_resource_t resource, isc_resourcevalue_t value) {
	struct rlimit rl;
	ISC_PLATFORM_RLIMITTYPE rlim_value;
	int unixresult;
	int unixresource;
	isc_result_t result;

	result = resource2rlim(resource, &unixresource);
	if (result != ISC_R_SUCCESS)
		return (result);

	if (value == ISC_RESOURCE_UNLIMITED)
		rlim_value = RLIM_INFINITY;

	else {
		/*
		 * isc_resourcevalue_t was chosen as an unsigned 64 bit
		 * integer so that it could contain the maximum range of
		 * reasonable values.  Unfortunately, this exceeds the typical
		 * range on Unix systems.  Ensure the range of
		 * ISC_PLATFORM_RLIMITTYPE is not overflowed.
		 */
		isc_resourcevalue_t rlim_max;
		isc_boolean_t rlim_t_is_signed =
			ISC_TF(((double)(ISC_PLATFORM_RLIMITTYPE)-1) < 0);

		if (rlim_t_is_signed)
			rlim_max = ~((ISC_PLATFORM_RLIMITTYPE)1 <<
				     (sizeof(ISC_PLATFORM_RLIMITTYPE) * 8 - 1));
		else
			rlim_max = (ISC_PLATFORM_RLIMITTYPE)-1;

		if (value > rlim_max)
			value = rlim_max;

		rlim_value = value;
	}

	rl.rlim_cur = rl.rlim_max = rlim_value;
	unixresult = setrlimit(unixresource, &rl);

	if (unixresult == 0)
		return (ISC_R_SUCCESS);

#if defined(OPEN_MAX) && defined(__APPLE__)
	/*
	 * The Darwin kernel doesn't accept RLIM_INFINITY for rlim_cur; the
	 * maximum possible value is OPEN_MAX.  BIND8 used to use
	 * sysconf(_SC_OPEN_MAX) for such a case, but this value is much
	 * smaller than OPEN_MAX and is not really effective.
	 */
	if (resource == isc_resource_openfiles && rlim_value == RLIM_INFINITY) {
		rl.rlim_cur = OPEN_MAX;
		unixresult = setrlimit(unixresource, &rl);
		if (unixresult == 0)
			return (ISC_R_SUCCESS);
	}
#elif defined(__linux__)
#ifndef NR_OPEN
#define NR_OPEN (1024*1024)
#endif

	/*
	 * Some Linux kernels don't accept RLIM_INFINIT; the maximum
	 * possible value is the NR_OPEN defined in linux/fs.h.
	 */
	if (resource == isc_resource_openfiles && rlim_value == RLIM_INFINITY) {
		rl.rlim_cur = rl.rlim_max = NR_OPEN;
		unixresult = setrlimit(unixresource, &rl);
		if (unixresult == 0)
			return (ISC_R_SUCCESS);
	}
#elif defined(__hpux) && defined(HAVE_SYS_DYNTUNE_H)
	if (resource == isc_resource_openfiles && rlim_value == RLIM_INFINITY) {
		uint64_t maxfiles;
		if (gettune("maxfiles_lim", &maxfiles) == 0) {
			rl.rlim_cur = rl.rlim_max = maxfiles;
			unixresult = setrlimit(unixresource, &rl);
			if (unixresult == 0)
				return (ISC_R_SUCCESS);
		}
	}
#endif
	if (resource == isc_resource_openfiles && rlim_value == RLIM_INFINITY) {
		if (getrlimit(unixresource, &rl) == 0) {
			rl.rlim_cur = rl.rlim_max;
			unixresult = setrlimit(unixresource, &rl);
			if (unixresult == 0)
				return (ISC_R_SUCCESS);
		}
	}
	return (isc__errno2result(errno));
}

isc_result_t
isc_resource_getlimit(isc_resource_t resource, isc_resourcevalue_t *value) {
	int unixresult;
	int unixresource;
	struct rlimit rl;
	isc_result_t result;

	result = resource2rlim(resource, &unixresource);
	if (result == ISC_R_SUCCESS) {
		unixresult = getrlimit(unixresource, &rl);
		INSIST(unixresult == 0);
		*value = rl.rlim_max;
	}

	return (result);
}

isc_result_t
isc_resource_getcurlimit(isc_resource_t resource, isc_resourcevalue_t *value) {
	int unixresult;
	int unixresource;
	struct rlimit rl;
	isc_result_t result;

	result = resource2rlim(resource, &unixresource);
	if (result == ISC_R_SUCCESS) {
		unixresult = getrlimit(unixresource, &rl);
		INSIST(unixresult == 0);
		*value = rl.rlim_cur;
	}

	return (result);
}
