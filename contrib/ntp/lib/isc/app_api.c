/*
 * Copyright (C) 2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: app_api.c,v 1.5 2009/09/02 23:48:02 tbox Exp $ */

#include <config.h>

#include <unistd.h>

#include <isc/app.h>
#include <isc/magic.h>
#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/util.h>

static isc_mutex_t createlock;
static isc_once_t once = ISC_ONCE_INIT;
static isc_appctxcreatefunc_t appctx_createfunc = NULL;

#define ISCAPI_APPMETHODS_VALID(m) ISC_MAGIC_VALID(m, ISCAPI_APPMETHODS_MAGIC)

static void
initialize(void) {
	RUNTIME_CHECK(isc_mutex_init(&createlock) == ISC_R_SUCCESS);
}

isc_result_t
isc_app_register(isc_appctxcreatefunc_t createfunc) {
	isc_result_t result = ISC_R_SUCCESS;

	RUNTIME_CHECK(isc_once_do(&once, initialize) == ISC_R_SUCCESS);

	LOCK(&createlock);
	if (appctx_createfunc == NULL)
		appctx_createfunc = createfunc;
	else
		result = ISC_R_EXISTS;
	UNLOCK(&createlock);

	return (result);
}

isc_result_t
isc_appctx_create(isc_mem_t *mctx, isc_appctx_t **ctxp) {
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(appctx_createfunc != NULL);
	result = (*appctx_createfunc)(mctx, ctxp);

	UNLOCK(&createlock);

	return (result);
}

void
isc_appctx_destroy(isc_appctx_t **ctxp) {
	REQUIRE(ctxp != NULL && ISCAPI_APPCTX_VALID(*ctxp));

	(*ctxp)->methods->ctxdestroy(ctxp);

	ENSURE(*ctxp == NULL);
}

isc_result_t
isc_app_ctxstart(isc_appctx_t *ctx) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));

	return (ctx->methods->ctxstart(ctx));
}

isc_result_t
isc_app_ctxrun(isc_appctx_t *ctx) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));

	return (ctx->methods->ctxrun(ctx));
}

isc_result_t
isc_app_ctxsuspend(isc_appctx_t *ctx) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));

	return (ctx->methods->ctxsuspend(ctx));
}

isc_result_t
isc_app_ctxshutdown(isc_appctx_t *ctx) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));

	return (ctx->methods->ctxshutdown(ctx));
}

void
isc_app_ctxfinish(isc_appctx_t *ctx) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));

	ctx->methods->ctxfinish(ctx);
}

void
isc_appctx_settaskmgr(isc_appctx_t *ctx, isc_taskmgr_t *taskmgr) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));
	REQUIRE(taskmgr != NULL);

	ctx->methods->settaskmgr(ctx, taskmgr);
}

void
isc_appctx_setsocketmgr(isc_appctx_t *ctx, isc_socketmgr_t *socketmgr) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));
	REQUIRE(socketmgr != NULL);

	ctx->methods->setsocketmgr(ctx, socketmgr);
}

void
isc_appctx_settimermgr(isc_appctx_t *ctx, isc_timermgr_t *timermgr) {
	REQUIRE(ISCAPI_APPCTX_VALID(ctx));
	REQUIRE(timermgr != NULL);

	ctx->methods->settimermgr(ctx, timermgr);
}
