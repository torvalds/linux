/*
 * Copyright (C) 2009, 2010  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: mem_api.c,v 1.8 2010/08/12 21:30:26 jinmei Exp $ */

#include <config.h>

#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/once.h>
#include <isc/util.h>

#if ISC_MEM_TRACKLINES
#define FLARG_PASS	, file, line
#define FLARG		, const char *file, unsigned int line
#else
#define FLARG_PASS
#define FLARG
#endif

static isc_mutex_t createlock;
static isc_once_t once = ISC_ONCE_INIT;
static isc_memcreatefunc_t mem_createfunc = NULL;

static void
initialize(void) {
	RUNTIME_CHECK(isc_mutex_init(&createlock) == ISC_R_SUCCESS);
}

isc_result_t
isc_mem_register(isc_memcreatefunc_t createfunc) {
	isc_result_t result = ISC_R_SUCCESS;

	RUNTIME_CHECK(isc_once_do(&once, initialize) == ISC_R_SUCCESS);

	LOCK(&createlock);
	if (mem_createfunc == NULL)
		mem_createfunc = createfunc;
	else
		result = ISC_R_EXISTS;
	UNLOCK(&createlock);

	return (result);
}

isc_result_t
isc_mem_create(size_t init_max_size, size_t target_size, isc_mem_t **mctxp) {
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(mem_createfunc != NULL);
	result = (*mem_createfunc)(init_max_size, target_size, mctxp,
				   ISC_MEMFLAG_DEFAULT);

	UNLOCK(&createlock);

	return (result);
}

isc_result_t
isc_mem_create2(size_t init_max_size, size_t target_size, isc_mem_t **mctxp,
		unsigned int flags)
{
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(mem_createfunc != NULL);
	result = (*mem_createfunc)(init_max_size, target_size, mctxp, flags);

	UNLOCK(&createlock);

	return (result);
}

void
isc_mem_attach(isc_mem_t *source, isc_mem_t **targetp) {
	REQUIRE(ISCAPI_MCTX_VALID(source));
	REQUIRE(targetp != NULL && *targetp == NULL);

	source->methods->attach(source, targetp);

	ENSURE(*targetp == source);
}

void
isc_mem_detach(isc_mem_t **mctxp) {
	REQUIRE(mctxp != NULL && ISCAPI_MCTX_VALID(*mctxp));

	(*mctxp)->methods->detach(mctxp);

	ENSURE(*mctxp == NULL);
}

void
isc_mem_destroy(isc_mem_t **mctxp) {
	REQUIRE(mctxp != NULL && ISCAPI_MCTX_VALID(*mctxp));

	(*mctxp)->methods->destroy(mctxp);

	ENSURE(*mctxp == NULL);
}

void *
isc__mem_get(isc_mem_t *mctx, size_t size FLARG) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->memget(mctx, size FLARG_PASS));
}

void
isc__mem_put(isc_mem_t *mctx, void *ptr, size_t size FLARG) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	mctx->methods->memput(mctx, ptr, size FLARG_PASS);
}

void
isc__mem_putanddetach(isc_mem_t **mctxp, void *ptr, size_t size FLARG) {
	REQUIRE(mctxp != NULL && ISCAPI_MCTX_VALID(*mctxp));

	(*mctxp)->methods->memputanddetach(mctxp, ptr, size FLARG_PASS);

	/*
	 * XXX: We cannot always ensure *mctxp == NULL here
	 * (see lib/isc/mem.c).
	 */
}

void *
isc__mem_allocate(isc_mem_t *mctx, size_t size FLARG) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->memallocate(mctx, size FLARG_PASS));
}

void *
isc__mem_reallocate(isc_mem_t *mctx, void *ptr, size_t size FLARG) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->memreallocate(mctx, ptr, size FLARG_PASS));
}

char *
isc__mem_strdup(isc_mem_t *mctx, const char *s FLARG) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->memstrdup(mctx, s FLARG_PASS));
}

void
isc__mem_free(isc_mem_t *mctx, void *ptr FLARG) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	mctx->methods->memfree(mctx, ptr FLARG_PASS);
}

void
isc_mem_setdestroycheck(isc_mem_t *mctx, isc_boolean_t flag) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	mctx->methods->setdestroycheck(mctx, flag);
}

void
isc_mem_setwater(isc_mem_t *ctx, isc_mem_water_t water, void *water_arg,
		 size_t hiwater, size_t lowater)
{
	REQUIRE(ISCAPI_MCTX_VALID(ctx));

	ctx->methods->setwater(ctx, water, water_arg, hiwater, lowater);
}

void
isc_mem_waterack(isc_mem_t *ctx, int flag) {
	REQUIRE(ISCAPI_MCTX_VALID(ctx));

	ctx->methods->waterack(ctx, flag);
}

size_t
isc_mem_inuse(isc_mem_t *mctx) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->inuse(mctx));
}

isc_boolean_t
isc_mem_isovermem(isc_mem_t *mctx) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->isovermem(mctx));
}

void
isc_mem_setname(isc_mem_t *mctx, const char *name, void *tag) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	UNUSED(name);
	UNUSED(tag);

	return;
}

const char *
isc_mem_getname(isc_mem_t *mctx) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return ("");
}

void *
isc_mem_gettag(isc_mem_t *mctx) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (NULL);
}

isc_result_t
isc_mempool_create(isc_mem_t *mctx, size_t size, isc_mempool_t **mpctxp) {
	REQUIRE(ISCAPI_MCTX_VALID(mctx));

	return (mctx->methods->mpcreate(mctx, size, mpctxp));
}

void
isc_mempool_destroy(isc_mempool_t **mpctxp) {
	REQUIRE(mpctxp != NULL && ISCAPI_MPOOL_VALID(*mpctxp));

	(*mpctxp)->methods->destroy(mpctxp);

	ENSURE(*mpctxp == NULL);
}

void *
isc__mempool_get(isc_mempool_t *mpctx FLARG) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	return (mpctx->methods->get(mpctx FLARG_PASS));
}

void
isc__mempool_put(isc_mempool_t *mpctx, void *mem FLARG) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	mpctx->methods->put(mpctx, mem FLARG_PASS);
}

unsigned int
isc_mempool_getallocated(isc_mempool_t *mpctx) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	return (mpctx->methods->getallocated(mpctx));
}

void
isc_mempool_setmaxalloc(isc_mempool_t *mpctx, unsigned int limit) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	mpctx->methods->setmaxalloc(mpctx, limit);
}

void
isc_mempool_setfreemax(isc_mempool_t *mpctx, unsigned int limit) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	mpctx->methods->setfreemax(mpctx, limit);
}

void
isc_mempool_setname(isc_mempool_t *mpctx, const char *name) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	mpctx->methods->setname(mpctx, name);
}

void
isc_mempool_associatelock(isc_mempool_t *mpctx, isc_mutex_t *lock) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	mpctx->methods->associatelock(mpctx, lock);
}

void
isc_mempool_setfillcount(isc_mempool_t *mpctx, unsigned int limit) {
	REQUIRE(ISCAPI_MPOOL_VALID(mpctx));

	mpctx->methods->setfillcount(mpctx, limit);
}
