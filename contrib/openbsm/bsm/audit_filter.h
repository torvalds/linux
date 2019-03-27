/*-
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _BSM_AUDIT_FILTER_H_
#define	_BSM_AUDIT_FILTER_H_

/*
 * Module interface for audit filter modules.
 *
 * audit_filter_attach_t - filter module is being attached with arguments
 * audit_filter_reinit_t - arguments to module have changed
 * audit_filter_record_t - present parsed record to filter module, with
 *                         receipt time
 * audit_filter_rawrecord_t - present BSM format record to filter module,
 *                            with receipt time
 * audit_filter_destach_t - filter module is being detached
 *
 * There may be many instances of the same filter, identified by the instance
 * void pointer maintained by the filter instance.
 */
typedef int (*audit_filter_attach_t)(void *instance, int argc, char *argv[]);
typedef int (*audit_filter_reinit_t)(void *instance, int argc, char *argv[]);
typedef void (*audit_filter_record_t)(void *instance, struct timespec *ts,
	    int token_count, const tokenstr_t tok[]);
typedef void (*audit_filter_rawrecord_t)(void *instance, struct timespec *ts,
	    void *data, u_int len);
typedef void (*audit_filter_detach_t)(void *instance);

/*
 * APIs that may be called by audit filters.
 */
void	audit_filter_getcookie(void *instance, void **cookie);
void	audit_filter_setcookie(void *instance, void *cookie);

/*
 * Values to be returned by audit_filter_init_t.
 */
#define	AUDIT_FILTER_SUCCESS	(0)
#define	AUDIT_FILTER_FAILURE	(-1)

/*
 * Standard name for filter module initialization functions, which will be
 * found using dlsym().
 */
#define	AUDIT_FILTER_ATTACH	audit_filter_attach
#define	AUDIT_FILTER_REINIT	audit_filter_reinit
#define	AUDIT_FILTER_RECORD	audit_filter_record
#define	AUDIT_FILTER_RAWRECORD	audit_filter_rawrecord
#define	AUDIT_FILTER_DETACH	audit_filter_detach
#define	AUDIT_FILTER_ATTACH_STRING	"audit_filter_attach"
#define	AUDIT_FILTER_REINIT_STRING	"audit_filter_reinit"
#define	AUDIT_FILTER_RECORD_STRING	"audit_filter_record"
#define	AUDIT_FILTER_RAWRECORD_STRING	"audit_filter_rawrecord"
#define	AUDIT_FILTER_DETACH_STRING	"audit_filter_detach"

#endif /* !_BSM_AUDIT_FILTER_H_ */
