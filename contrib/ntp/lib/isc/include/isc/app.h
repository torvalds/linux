/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

/* $Id: app.h,v 1.11 2009/09/02 23:48:03 tbox Exp $ */

#ifndef ISC_APP_H
#define ISC_APP_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/app.h
 * \brief ISC Application Support
 *
 * Dealing with program termination can be difficult, especially in a
 * multithreaded program.  The routines in this module help coordinate
 * the shutdown process.  They are used as follows by the initial (main)
 * thread of the application:
 *
 *\li		isc_app_start();	Call very early in main(), before
 *					any other threads have been created.
 *
 *\li		isc_app_run();		This will post any on-run events,
 *					and then block until application
 *					shutdown is requested.  A shutdown
 *					request is made by calling
 *					isc_app_shutdown(), or by sending
 *					SIGINT or SIGTERM to the process.
 *					After isc_app_run() returns, the
 *					application should shutdown itself.
 *
 *\li		isc_app_finish();	Call very late in main().
 *
 * Applications that want to use SIGHUP/isc_app_reload() to trigger reloading
 * should check the result of isc_app_run() and call the reload routine if
 * the result is ISC_R_RELOAD.  They should then call isc_app_run() again
 * to resume waiting for reload or termination.
 *
 * Use of this module is not required.  In particular, isc_app_start() is
 * NOT an ISC library initialization routine.
 *
 * This module also supports per-thread 'application contexts'.  With this
 * mode, a thread-based application will have a separate context, in which
 * it uses other ISC library services such as tasks or timers.  Signals are
 * not caught in this mode, so that the application can handle the signals
 * in its preferred way.
 *
 * \li MP:
 *	Clients must ensure that isc_app_start(), isc_app_run(), and
 *	isc_app_finish() are called at most once.  isc_app_shutdown()
 *	is safe to use by any thread (provided isc_app_start() has been
 *	called previously).
 *
 *	The same note applies to isc_app_ctxXXX() functions, but in this case
 *	it's a per-thread restriction.  For example, a thread with an
 *	application context must ensure that isc_app_ctxstart() with the
 *	context is called at most once.
 *
 * \li Reliability:
 *	No anticipated impact.
 *
 * \li Resources:
 *	None.
 *
 * \li Security:
 *	No anticipated impact.
 *
 * \li Standards:
 *	None.
 */

#include <isc/eventclass.h>
#include <isc/lang.h>
#include <isc/magic.h>
#include <isc/result.h>

/***
 *** Types
 ***/

typedef isc_event_t isc_appevent_t;

#define ISC_APPEVENT_FIRSTEVENT		(ISC_EVENTCLASS_APP + 0)
#define ISC_APPEVENT_SHUTDOWN		(ISC_EVENTCLASS_APP + 1)
#define ISC_APPEVENT_LASTEVENT		(ISC_EVENTCLASS_APP + 65535)

/*%
 * app module methods.  Only app driver implementations use this structure.
 * Other clients should use the top-level interfaces (i.e., isc_app_xxx
 * functions).  magic must be ISCAPI_APPMETHODS_MAGIC.
 */
typedef struct isc_appmethods {
	void		(*ctxdestroy)(isc_appctx_t **ctxp);
	isc_result_t	(*ctxstart)(isc_appctx_t *ctx);
	isc_result_t	(*ctxrun)(isc_appctx_t *ctx);
	isc_result_t	(*ctxsuspend)(isc_appctx_t *ctx);
	isc_result_t	(*ctxshutdown)(isc_appctx_t *ctx);
	void		(*ctxfinish)(isc_appctx_t *ctx);
	void		(*settaskmgr)(isc_appctx_t *ctx,
				      isc_taskmgr_t *timermgr);
	void		(*setsocketmgr)(isc_appctx_t *ctx,
					isc_socketmgr_t *timermgr);
	void		(*settimermgr)(isc_appctx_t *ctx,
				       isc_timermgr_t *timermgr);
} isc_appmethods_t;

/*%
 * This structure is actually just the common prefix of an application context
 * implementation's version of an isc_appctx_t.
 * \brief
 * Direct use of this structure by clients is forbidden.  app implementations
 * may change the structure.  'magic' must be ISCAPI_APPCTX_MAGIC for any
 * of the isc_app_ routines to work.  app implementations must maintain
 * all app context invariants.
 */
struct isc_appctx {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_appmethods_t	*methods;
};

#define ISCAPI_APPCTX_MAGIC		ISC_MAGIC('A','a','p','c')
#define ISCAPI_APPCTX_VALID(c)		((c) != NULL && \
					 (c)->magic == ISCAPI_APPCTX_MAGIC)

ISC_LANG_BEGINDECLS

isc_result_t
isc_app_ctxstart(isc_appctx_t *ctx);

isc_result_t
isc_app_start(void);
/*!<
 * \brief Start an ISC library application.
 *
 * Notes:
 *	This call should be made before any other ISC library call, and as
 *	close to the beginning of the application as possible.
 *
 * Requires:
 *	'ctx' is a valid application context (for app_ctxstart()).
 */

isc_result_t
isc_app_onrun(isc_mem_t *mctx, isc_task_t *task, isc_taskaction_t action,
	      void *arg);
/*!<
 * \brief Request delivery of an event when the application is run.
 *
 * Requires:
 *\li	isc_app_start() has been called.
 *
 * Returns:
 *	ISC_R_SUCCESS
 *	ISC_R_NOMEMORY
 */

isc_result_t
isc_app_ctxrun(isc_appctx_t *ctx);

isc_result_t
isc_app_run(void);
/*!<
 * \brief Run an ISC library application.
 *
 * Notes:
 *\li	The caller (typically the initial thread of an application) will
 *	block until shutdown is requested.  When the call returns, the
 *	caller should start shutting down the application.
 *
 * Requires:
 *\li	isc_app_[ctx]start() has been called.
 *
 * Ensures:
 *\li	Any events requested via isc_app_onrun() will have been posted (in
 *	FIFO order) before isc_app_run() blocks.
 *\li	'ctx' is a valid application context (for app_ctxrun()).
 *
 * Returns:
 *\li	ISC_R_SUCCESS			Shutdown has been requested.
 *\li	ISC_R_RELOAD			Reload has been requested.
 */

isc_result_t
isc_app_ctxshutdown(isc_appctx_t *ctx);

isc_result_t
isc_app_shutdown(void);
/*!<
 * \brief Request application shutdown.
 *
 * Notes:
 *\li	It is safe to call isc_app_shutdown() multiple times.  Shutdown will
 *	only be triggered once.
 *
 * Requires:
 *\li	isc_app_[ctx]run() has been called.
 *\li	'ctx' is a valid application context (for app_ctxshutdown()).
 *
 * Returns:
 *\li	ISC_R_SUCCESS
 *\li	ISC_R_UNEXPECTED
 */

isc_result_t
isc_app_ctxsuspend(isc_appctx_t *ctx);
/*!<
 * \brief This has the same behavior as isc_app_ctxsuspend().
 */

isc_result_t
isc_app_reload(void);
/*!<
 * \brief Request application reload.
 *
 * Requires:
 *\li	isc_app_run() has been called.
 *
 * Returns:
 *\li	ISC_R_SUCCESS
 *\li	ISC_R_UNEXPECTED
 */

void
isc_app_ctxfinish(isc_appctx_t *ctx);

void
isc_app_finish(void);
/*!<
 * \brief Finish an ISC library application.
 *
 * Notes:
 *\li	This call should be made at or near the end of main().
 *
 * Requires:
 *\li	isc_app_start() has been called.
 *\li	'ctx' is a valid application context (for app_ctxfinish()).
 *
 * Ensures:
 *\li	Any resources allocated by isc_app_start() have been released.
 */

void
isc_app_block(void);
/*!<
 * \brief Indicate that a blocking operation will be performed.
 *
 * Notes:
 *\li	If a blocking operation is in process, a call to isc_app_shutdown()
 *	or an external signal will abort the program, rather than allowing
 *	clean shutdown.  This is primarily useful for reading user input.
 *
 * Requires:
 * \li	isc_app_start() has been called.
 * \li	No other blocking operations are in progress.
 */

void
isc_app_unblock(void);
/*!<
 * \brief Indicate that a blocking operation is complete.
 *
 * Notes:
 * \li	When a blocking operation has completed, return the program to a
 * 	state where a call to isc_app_shutdown() or an external signal will
 * 	shutdown normally.
 *
 * Requires:
 * \li	isc_app_start() has been called.
 * \li	isc_app_block() has been called by the same thread.
 */

isc_result_t
isc_appctx_create(isc_mem_t *mctx, isc_appctx_t **ctxp);
/*!<
 * \brief Create an application context.
 *
 * Requires:
 *\li	'mctx' is a valid memory context.
 *\li	'ctxp' != NULL && *ctxp == NULL.
 */

void
isc_appctx_destroy(isc_appctx_t **ctxp);
/*!<
 * \brief Destroy an application context.
 *
 * Requires:
 *\li	'*ctxp' is a valid application context.
 *
 * Ensures:
 *\li	*ctxp == NULL.
 */

void
isc_appctx_settaskmgr(isc_appctx_t *ctx, isc_taskmgr_t *taskmgr);
/*!<
 * \brief Associate a task manager with an application context.
 *
 * This must be done before running tasks within the application context.
 *
 * Requires:
 *\li	'ctx' is a valid application context.
 *\li	'taskmgr' is a valid task manager.
 */

void
isc_appctx_setsocketmgr(isc_appctx_t *ctx, isc_socketmgr_t *socketmgr);
/*!<
 * \brief Associate a socket manager with an application context.
 *
 * This must be done before handling socket events within the application
 * context.
 *
 * Requires:
 *\li	'ctx' is a valid application context.
 *\li	'socketmgr' is a valid socket manager.
 */

void
isc_appctx_settimermgr(isc_appctx_t *ctx, isc_timermgr_t *timermgr);
/*!<
 * \brief Associate a socket timer with an application context.
 *
 * This must be done before handling timer events within the application
 * context.
 *
 * Requires:
 *\li	'ctx' is a valid application context.
 *\li	'timermgr' is a valid timer manager.
 */

#ifdef USE_APPIMPREGISTER
/*%<
 * See isc_appctx_create() above.
 */
typedef isc_result_t
(*isc_appctxcreatefunc_t)(isc_mem_t *mctx, isc_appctx_t **ctxp);

isc_result_t
isc_app_register(isc_appctxcreatefunc_t createfunc);
/*%<
 * Register a new application implementation and add it to the list of
 * supported implementations.  This function must be called when a different
 * event library is used than the one contained in the ISC library.
 */

isc_result_t
isc__app_register(void);
/*%<
 * A short cut function that specifies the application module in the ISC
 * library for isc_app_register().  An application that uses the ISC library
 * usually do not have to care about this function: it would call
 * isc_lib_register(), which internally calls this function.
 */
#endif /* USE_APPIMPREGISTER */

ISC_LANG_ENDDECLS

#endif /* ISC_APP_H */
