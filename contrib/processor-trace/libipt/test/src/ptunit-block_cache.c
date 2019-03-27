/*
 * Copyright (c) 2016-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ptunit_threads.h"

#include "pt_block_cache.h"

#include <string.h>


/* A test fixture optionally providing a block cache and automatically freeing
 * the cache.
 */
struct bcache_fixture {
	/* Threading support. */
	struct ptunit_thrd_fixture thrd;

	/* The cache - it will be freed automatically. */
	struct pt_block_cache *bcache;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct bcache_fixture *);
	struct ptunit_result (*fini)(struct bcache_fixture *);
};

enum {
	/* The number of entries in fixture-provided caches. */
	bfix_nentries = 0x10000,

#if defined(FEATURE_THREADS)

	/* The number of additional threads to use for stress testing. */
	bfix_threads = 3,

#endif /* defined(FEATURE_THREADS) */

	/* The number of iterations in stress testing. */
	bfix_iterations = 0x10
};

static struct ptunit_result cfix_init(struct bcache_fixture *bfix)
{
	ptu_test(ptunit_thrd_init, &bfix->thrd);

	bfix->bcache = NULL;

	return ptu_passed();
}

static struct ptunit_result bfix_init(struct bcache_fixture *bfix)
{
	ptu_test(cfix_init, bfix);

	bfix->bcache = pt_bcache_alloc(bfix_nentries);
	ptu_ptr(bfix->bcache);

	return ptu_passed();
}

static struct ptunit_result bfix_fini(struct bcache_fixture *bfix)
{
	int thrd;

	ptu_test(ptunit_thrd_fini, &bfix->thrd);

	for (thrd = 0; thrd < bfix->thrd.nthreads; ++thrd)
		ptu_int_eq(bfix->thrd.result[thrd], 0);

	pt_bcache_free(bfix->bcache);

	return ptu_passed();
}

static struct ptunit_result bcache_entry_size(void)
{
	ptu_uint_eq(sizeof(struct pt_bcache_entry), sizeof(uint32_t));

	return ptu_passed();
}

static struct ptunit_result bcache_size(void)
{
	ptu_uint_le(sizeof(struct pt_block_cache),
		    2 * sizeof(struct pt_bcache_entry));

	return ptu_passed();
}

static struct ptunit_result free_null(void)
{
	pt_bcache_free(NULL);

	return ptu_passed();
}

static struct ptunit_result add_null(void)
{
	struct pt_bcache_entry bce;
	int errcode;

	memset(&bce, 0, sizeof(bce));

	errcode = pt_bcache_add(NULL, 0ull, bce);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result lookup_null(void)
{
	struct pt_bcache_entry bce;
	struct pt_block_cache bcache;
	int errcode;

	errcode = pt_bcache_lookup(&bce, NULL, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_bcache_lookup(NULL, &bcache, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result alloc(struct bcache_fixture *bfix)
{
	bfix->bcache = pt_bcache_alloc(0x10000ull);
	ptu_ptr(bfix->bcache);

	return ptu_passed();
}

static struct ptunit_result alloc_min(struct bcache_fixture *bfix)
{
	bfix->bcache = pt_bcache_alloc(1ull);
	ptu_ptr(bfix->bcache);

	return ptu_passed();
}

static struct ptunit_result alloc_too_big(struct bcache_fixture *bfix)
{
	bfix->bcache = pt_bcache_alloc(UINT32_MAX + 1ull);
	ptu_null(bfix->bcache);

	return ptu_passed();
}

static struct ptunit_result alloc_zero(struct bcache_fixture *bfix)
{
	bfix->bcache = pt_bcache_alloc(0ull);
	ptu_null(bfix->bcache);

	return ptu_passed();
}

static struct ptunit_result initially_empty(struct bcache_fixture *bfix)
{
	uint64_t index;

	for (index = 0; index < bfix_nentries; ++index) {
		struct pt_bcache_entry bce;
		int status;

		memset(&bce, 0xff, sizeof(bce));

		status = pt_bcache_lookup(&bce, bfix->bcache, index);
		ptu_int_eq(status, 0);

		status = pt_bce_is_valid(bce);
		ptu_int_eq(status, 0);
	}

	return ptu_passed();
}

static struct ptunit_result add_bad_index(struct bcache_fixture *bfix)
{
	struct pt_bcache_entry bce;
	int errcode;

	memset(&bce, 0, sizeof(bce));

	errcode = pt_bcache_add(bfix->bcache, bfix_nentries, bce);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result lookup_bad_index(struct bcache_fixture *bfix)
{
	struct pt_bcache_entry bce;
	int errcode;

	errcode = pt_bcache_lookup(&bce, bfix->bcache, bfix_nentries);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result add(struct bcache_fixture *bfix, uint64_t index)
{
	struct pt_bcache_entry bce, exp;
	int errcode;

	memset(&bce, 0xff, sizeof(bce));
	memset(&exp, 0x00, sizeof(exp));

	exp.ninsn = 1;
	exp.displacement = 7;
	exp.mode = ptem_64bit;
	exp.qualifier = ptbq_decode;
	exp.isize = 7;

	errcode = pt_bcache_add(bfix->bcache, index, exp);
	ptu_int_eq(errcode, 0);

	errcode = pt_bcache_lookup(&bce, bfix->bcache, index);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(bce.ninsn, exp.ninsn);
	ptu_int_eq(bce.displacement, exp.displacement);
	ptu_uint_eq(pt_bce_exec_mode(bce), pt_bce_exec_mode(exp));
	ptu_uint_eq(pt_bce_qualifier(bce), pt_bce_qualifier(exp));
	ptu_uint_eq(bce.isize, exp.isize);

	return ptu_passed();
}

static int worker(void *arg)
{
	struct pt_bcache_entry exp;
	struct pt_block_cache *bcache;
	uint64_t iter, index;

	bcache = arg;
	if (!bcache)
		return -pte_internal;

	memset(&exp, 0x00, sizeof(exp));
	exp.ninsn = 5;
	exp.displacement = 28;
	exp.mode = ptem_64bit;
	exp.qualifier = ptbq_again;
	exp.isize = 3;

	for (index = 0; index < bfix_nentries; ++index) {
		for (iter = 0; iter < bfix_iterations; ++iter) {
			struct pt_bcache_entry bce;
			int errcode;

			memset(&bce, 0xff, sizeof(bce));

			errcode = pt_bcache_lookup(&bce, bcache, index);
			if (errcode < 0)
				return errcode;

			if (!pt_bce_is_valid(bce)) {
				errcode = pt_bcache_add(bcache, index, exp);
				if (errcode < 0)
					return errcode;
			}

			errcode = pt_bcache_lookup(&bce, bcache, index);
			if (errcode < 0)
				return errcode;

			if (!pt_bce_is_valid(bce))
				return -pte_nosync;

			if (bce.ninsn != exp.ninsn)
				return -pte_nosync;

			if (bce.displacement != exp.displacement)
				return -pte_nosync;

			if (pt_bce_exec_mode(bce) != pt_bce_exec_mode(exp))
				return -pte_nosync;

			if (pt_bce_qualifier(bce) != pt_bce_qualifier(exp))
				return -pte_nosync;

			if (bce.isize != exp.isize)
				return -pte_nosync;
		}
	}

	return 0;
}

static struct ptunit_result stress(struct bcache_fixture *bfix)
{
	int errcode;

#if defined(FEATURE_THREADS)
	{
		int thrd;

		for (thrd = 0; thrd < bfix_threads; ++thrd)
			ptu_test(ptunit_thrd_create, &bfix->thrd, worker,
				 bfix->bcache);
	}
#endif /* defined(FEATURE_THREADS) */

	errcode = worker(bfix->bcache);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct bcache_fixture bfix, cfix;
	struct ptunit_suite suite;

	bfix.init = bfix_init;
	bfix.fini = bfix_fini;

	cfix.init = cfix_init;
	cfix.fini = bfix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, bcache_entry_size);
	ptu_run(suite, bcache_size);

	ptu_run(suite, free_null);
	ptu_run(suite, add_null);
	ptu_run(suite, lookup_null);

	ptu_run_f(suite, alloc, cfix);
	ptu_run_f(suite, alloc_min, cfix);
	ptu_run_f(suite, alloc_too_big, cfix);
	ptu_run_f(suite, alloc_zero, cfix);

	ptu_run_f(suite, initially_empty, bfix);

	ptu_run_f(suite, add_bad_index, bfix);
	ptu_run_f(suite, lookup_bad_index, bfix);

	ptu_run_fp(suite, add, bfix, 0ull);
	ptu_run_fp(suite, add, bfix, bfix_nentries - 1ull);
	ptu_run_f(suite, stress, bfix);

	return ptunit_report(&suite);
}
