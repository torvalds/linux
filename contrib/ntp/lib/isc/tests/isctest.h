/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

/*! \file */

#include <config.h>

#include <isc/buffer.h>
#include <isc/entropy.h>
#include <isc/hash.h>
#include <isc/log.h>
#include <isc/mem.h>
#include <isc/print.h>
#include <isc/result.h>
#include <isc/string.h>
#include <isc/task.h>
#include <isc/timer.h>
#include <isc/util.h>

#define CHECK(r) \
	do { \
		result = (r); \
		if (result != ISC_R_SUCCESS) \
			goto cleanup; \
	} while (0)

extern isc_mem_t *mctx;
extern isc_entropy_t *ectx;
extern isc_log_t *lctx;
extern isc_taskmgr_t *taskmgr;
isc_timermgr_t *timermgr;
isc_socketmgr_t *socketmgr;
extern int ncpus;

isc_result_t
isc_test_begin(FILE *logfile, isc_boolean_t start_managers);

void
isc_test_end(void);

void
isc_test_nap(isc_uint32_t usec);
