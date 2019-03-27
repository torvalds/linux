/* $NetBSD: t_uvm_physseg_load.c,v 1.2 2016/12/22 08:15:20 cherry Exp $ */

/*-
 * Copyright (c) 2015, 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Santhosh N. Raju <santhosh.raju@gmail.com> and
 * by Cherry G. Mathew
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_uvm_physseg_load.c,v 1.2 2016/12/22 08:15:20 cherry Exp $");

/*
 * If this line is commented out tests related touvm_physseg_get_pmseg()
 * wont run.
 *
 * Have a look at machine/uvm_physseg.h for more details.
 */
#define __HAVE_PMAP_PHYSSEG

/*
 * This is a dummy struct used for testing purposes
 *
 * In reality this struct would exist in the MD part of the code residing in
 * machines/vmparam.h
 */

#ifdef __HAVE_PMAP_PHYSSEG
struct pmap_physseg {
	int dummy_variable;		/* Dummy variable use for testing */
};
#endif

/* Testing API - assumes userland */
/* Provide Kernel API equivalents */
#include <assert.h>
#include <stdbool.h>
#include <string.h> /* memset(3) et. al */
#include <stdio.h> /* printf(3) */
#include <stdlib.h> /* malloc(3) */
#include <stdarg.h>
#include <stddef.h>
#include <time.h>

#define	PRIxPADDR	"lx"
#define	PRIxPSIZE	"lx"
#define	PRIuPSIZE	"lu"
#define	PRIxVADDR	"lx"
#define	PRIxVSIZE	"lx"
#define	PRIuVSIZE	"lu"

#define UVM_HOTPLUG /* Enable hotplug with rbtree. */
#define PMAP_STEAL_MEMORY
#define DEBUG /* Enable debug functionality. */

typedef unsigned long vaddr_t;
typedef unsigned long paddr_t;
typedef unsigned long psize_t;
typedef unsigned long vsize_t;

#include <uvm/uvm_physseg.h>
#include <uvm/uvm_page.h>

#ifndef DIAGNOSTIC
#define	KASSERTMSG(e, msg, ...)	/* NOTHING */
#define	KASSERT(e)		/* NOTHING */
#else
#define	KASSERT(a)		assert(a)
#define KASSERTMSG(exp, ...)    printf(__VA_ARGS__); assert((exp))
#endif

#define VM_PHYSSEG_STRAT VM_PSTRAT_BSEARCH

#define VM_NFREELIST            4
#define VM_FREELIST_DEFAULT     0
#define VM_FREELIST_FIRST16     3
#define VM_FREELIST_FIRST1G     2
#define VM_FREELIST_FIRST4G     1

/*
 * Used in tests when Array implementation is tested
 */
#if !defined(VM_PHYSSEG_MAX)
#define VM_PHYSSEG_MAX          32
#endif

#define PAGE_SIZE               4096
#define PAGE_SHIFT              12
#define atop(x)         (((paddr_t)(x)) >> PAGE_SHIFT)

#define	mutex_enter(l)
#define	mutex_exit(l)

#define	_SYS_KMEM_H_ /* Disallow the real kmem API (see below) */
/* free(p) XXX: pgs management need more thought */
#define kmem_alloc(size, flags) malloc(size)
#define kmem_zalloc(size, flags) malloc(size)
#define kmem_free(p, size) free(p)

psize_t physmem;

struct uvmexp uvmexp;        /* decl */

/*
 * uvm structure borrowed from uvm.h
 *
 * Remember this is a dummy structure used within the ATF Tests and
 * uses only necessary fields from the original uvm struct.
 * See uvm/uvm.h for the full struct.
 */

struct uvm {
	/* vm_page related parameters */

	bool page_init_done;		/* TRUE if uvm_page_init() finished */
} uvm;

static void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	KASSERT(false);

	/*NOTREACHED*/
}

static void
uvm_pagefree(struct vm_page *pg)
{
	return;
}

#if defined(UVM_HOTPLUG)
static void
uvmpdpol_reinit(void)
{
	return;
}
#endif /* UVM_HOTPLUG */

/* end - Provide Kernel API equivalents */

#include "uvm/uvm_physseg.c"

#include <atf-c.h>

#define ONE_MEGABYTE 1024 * 1024

/* Sample Page Frame Numbers */
#define VALID_START_PFN_1 atop(0)
#define VALID_END_PFN_1 atop(ONE_MEGABYTE)
#define VALID_AVAIL_START_PFN_1 atop(0)
#define VALID_AVAIL_END_PFN_1 atop(ONE_MEGABYTE)

#define VALID_START_PFN_2 atop(ONE_MEGABYTE + 1)
#define VALID_END_PFN_2 atop(ONE_MEGABYTE * 2)
#define VALID_AVAIL_START_PFN_2 atop(ONE_MEGABYTE + 1)
#define VALID_AVAIL_END_PFN_2 atop(ONE_MEGABYTE * 2)

#define VALID_START_PFN_3 atop((ONE_MEGABYTE * 2) + 1)
#define VALID_END_PFN_3 atop(ONE_MEGABYTE * 3)
#define VALID_AVAIL_START_PFN_3 atop((ONE_MEGABYTE * 2) + 1)
#define VALID_AVAIL_END_PFN_3 atop(ONE_MEGABYTE * 3)

#define VALID_START_PFN_4 atop(ONE_MEGABYTE + 1)
#define VALID_END_PFN_4 atop(ONE_MEGABYTE * 128)
#define VALID_AVAIL_START_PFN_4 atop(ONE_MEGABYTE + 1)
#define VALID_AVAIL_END_PFN_4 atop(ONE_MEGABYTE * 128)

#define VALID_START_PFN_5 atop(ONE_MEGABYTE + 1)
#define VALID_END_PFN_5 atop(ONE_MEGABYTE * 256)
#define VALID_AVAIL_START_PFN_5 atop(ONE_MEGABYTE + 1)
#define VALID_AVAIL_END_PFN_5 atop(ONE_MEGABYTE * 256)

/*
 * Total number of pages (of 4K size each) should be 256 for 1MB of memory.
 */
#define PAGE_COUNT_1M      256

/*
 * The number of Page Frames to allot per segment
 */
#define PF_STEP 8

/*
 * A debug fucntion to print the content of upm.
 */
	static inline void
	uvm_physseg_dump_seg(uvm_physseg_t upm)
	{
#if defined(DEBUG)
		printf("%s: seg->start == %ld\n", __func__,
		    uvm_physseg_get_start(upm));
		printf("%s: seg->end == %ld\n", __func__,
		    uvm_physseg_get_end(upm));
		printf("%s: seg->avail_start == %ld\n", __func__,
		    uvm_physseg_get_avail_start(upm));
		printf("%s: seg->avail_end == %ld\n", __func__,
		    uvm_physseg_get_avail_end(upm));

		printf("====\n\n");
#else
		return;
#endif /* DEBUG */
	}

/*
 * Private accessor that gets the value of vm_physmem.nentries
 */
static int
uvm_physseg_get_entries(void)
{
#if defined(UVM_HOTPLUG)
	return uvm_physseg_graph.nentries;
#else
	return vm_nphysmem;
#endif /* UVM_HOTPLUG */
}

/*
 * Note: This function replicates verbatim what happens in
 * uvm_page.c:uvm_page_init().
 *
 * Please track any changes that happen there.
 */
static void
uvm_page_init_fake(struct vm_page *pagearray, psize_t pagecount)
{
	uvm_physseg_t bank;
	size_t n;

	for (bank = uvm_physseg_get_first(),
		 uvm_physseg_seg_chomp_slab(bank, pagearray, pagecount);
	     uvm_physseg_valid_p(bank);
	     bank = uvm_physseg_get_next(bank)) {

		n = uvm_physseg_get_end(bank) - uvm_physseg_get_start(bank);
		uvm_physseg_seg_alloc_from_slab(bank, n);
		uvm_physseg_init_seg(bank, pagearray);

		/* set up page array pointers */
		pagearray += n;
		pagecount -= n;
	}

	uvm.page_init_done = true;
}

/*
 * PHYS_TO_VM_PAGE: find vm_page for a PA.   used by MI code to get vm_pages
 * back from an I/O mapping (ugh!).   used in some MD code as well.
 */
static struct vm_page *
uvm_phys_to_vm_page(paddr_t pa)
{
	paddr_t pf = atop(pa);
	paddr_t off;
	uvm_physseg_t psi;

	psi = uvm_physseg_find(pf, &off);
	if (psi != UVM_PHYSSEG_TYPE_INVALID)
		return uvm_physseg_get_pg(psi, off);
	return(NULL);
}

//static paddr_t
//uvm_vm_page_to_phys(const struct vm_page *pg)
//{
//
//	return pg->phys_addr;
//}

/*
 * XXX: To do, write control test cases for uvm_vm_page_to_phys().
 */

/* #define VM_PAGE_TO_PHYS(entry)  uvm_vm_page_to_phys(entry) */

#define PHYS_TO_VM_PAGE(pa)     uvm_phys_to_vm_page(pa)

/*
 * Test Fixture SetUp().
 */
static void
setup(void)
{
	/* Prerequisites for running certain calls in uvm_physseg */
	uvmexp.pagesize = PAGE_SIZE;
	uvmexp.npages = 0;
	uvm.page_init_done = false;
	uvm_physseg_init();
}

ATF_TC(uvm_physseg_100);
ATF_TC_HEAD(uvm_physseg_100, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    100 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_100, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 100; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_1K);
ATF_TC_HEAD(uvm_physseg_1K, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    1000 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_1K, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 1000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_10K);
ATF_TC_HEAD(uvm_physseg_10K, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    10,000 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_10K, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 10000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_100K);
ATF_TC_HEAD(uvm_physseg_100K, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    100,000 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_100K, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 100000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_1M);
ATF_TC_HEAD(uvm_physseg_1M, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    1,000,000 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_1M, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 1000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_10M);
ATF_TC_HEAD(uvm_physseg_10M, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    10,000,000 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_10M, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 10000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_100M);
ATF_TC_HEAD(uvm_physseg_100M, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    100,000,000 calls, VM_PHYSSEG_MAX is 32.");
}
ATF_TC_BODY(uvm_physseg_100M, tc)
{
	paddr_t pa;

	setup();

	for(paddr_t i = VALID_START_PFN_1;
	    i < VALID_END_PFN_1; i += PF_STEP) {
		uvm_page_physload(i, i + PF_STEP, i, i + PF_STEP,
		    VM_FREELIST_DEFAULT);
	}

	ATF_REQUIRE_EQ(VM_PHYSSEG_MAX, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(int i = 0; i < 100000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_1);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_1MB);
ATF_TC_HEAD(uvm_physseg_1MB, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    10,000,000 calls, VM_PHYSSEG_MAX is 32 on 1 MB Segment.");
}
ATF_TC_BODY(uvm_physseg_1MB, t)
{
	paddr_t pa = 0;

	paddr_t pf = 0;

	psize_t pf_chunk_size = 0;

	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);

	psize_t npages2 = (VALID_END_PFN_2 - VALID_START_PFN_2);

	struct vm_page *slab = malloc(sizeof(struct vm_page) *
	    (npages1 + npages2));

	setup();

	/* We start with zero segments */
	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_1, npages1, NULL));
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2);

	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_2, npages2, NULL));
	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(pf = VALID_START_PFN_2; pf < VALID_END_PFN_2; pf += PF_STEP) {
		pf_chunk_size = (psize_t) random() % (psize_t) (PF_STEP - 1) + 1;
		uvm_physseg_unplug(pf, pf_chunk_size);
	}

	for(int i = 0; i < 10000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_2);
		if(pa < ctob(VALID_START_PFN_2))
			pa += ctob(VALID_START_PFN_2);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_64MB);
ATF_TC_HEAD(uvm_physseg_64MB, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    10,000,000 calls, VM_PHYSSEG_MAX is 32 on 64 MB Segment.");
}
ATF_TC_BODY(uvm_physseg_64MB, t)
{
	paddr_t pa = 0;

	paddr_t pf = 0;

	psize_t pf_chunk_size = 0;

	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);

	psize_t npages2 = (VALID_END_PFN_3 - VALID_START_PFN_3);

	struct vm_page *slab = malloc(sizeof(struct vm_page)  *
	    (npages1 + npages2));

	setup();

	/* We start with zero segments */
	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_1, npages1, NULL));
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2);

	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_3, npages2, NULL));
	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(pf = VALID_START_PFN_3; pf < VALID_END_PFN_3; pf += PF_STEP) {
		pf_chunk_size = (psize_t) random() % (psize_t) (PF_STEP - 1) + 1;
		uvm_physseg_unplug(pf, pf_chunk_size);
	}

	for(int i = 0; i < 10000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_3);
		if(pa < ctob(VALID_START_PFN_3))
			pa += ctob(VALID_START_PFN_3);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_128MB);
ATF_TC_HEAD(uvm_physseg_128MB, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    10,000,000 calls, VM_PHYSSEG_MAX is 32 on 128 MB Segment.");
}
ATF_TC_BODY(uvm_physseg_128MB, t)
{
	paddr_t pa = 0;

	paddr_t pf = 0;

	psize_t pf_chunk_size = 0;

	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);

	psize_t npages2 = (VALID_END_PFN_4 - VALID_START_PFN_4);

	struct vm_page *slab = malloc(sizeof(struct vm_page)
	    * (npages1 + npages2));

	setup();

	/* We start with zero segments */
	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_1, npages1, NULL));
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2);

	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_2, npages2, NULL));
	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(pf = VALID_START_PFN_4; pf < VALID_END_PFN_4; pf += PF_STEP) {
		pf_chunk_size = (psize_t) random() % (psize_t) (PF_STEP - 1) + 1;
		uvm_physseg_unplug(pf, pf_chunk_size);
	}

	for(int i = 0; i < 10000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_4);
		if(pa < ctob(VALID_START_PFN_4))
			pa += ctob(VALID_START_PFN_4);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TC(uvm_physseg_256MB);
ATF_TC_HEAD(uvm_physseg_256MB, tc)
{
	atf_tc_set_md_var(tc, "descr", "Load test uvm_phys_to_vm_page() with \
	    10,000,000 calls, VM_PHYSSEG_MAX is 32 on 256 MB Segment.");
}
ATF_TC_BODY(uvm_physseg_256MB, t)
{
	paddr_t pa = 0;

	paddr_t pf = 0;

	psize_t pf_chunk_size = 0;

	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);

	psize_t npages2 = (VALID_END_PFN_5 - VALID_START_PFN_5);

	struct vm_page *slab = malloc(sizeof(struct vm_page)  * (npages1 + npages2));

	setup();

	/* We start with zero segments */
	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_1, npages1, NULL));
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2);

	ATF_REQUIRE_EQ(true, uvm_physseg_plug(VALID_START_PFN_2, npages2, NULL));
	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	srandom((unsigned)time(NULL));
	for(pf = VALID_START_PFN_5; pf < VALID_END_PFN_5; pf += PF_STEP) {
		pf_chunk_size = (psize_t) random() % (psize_t) (PF_STEP - 1) + 1;
		uvm_physseg_unplug(pf, pf_chunk_size);
	}

	for(int i = 0; i < 10000000; i++) {
		pa = (paddr_t) random() % (paddr_t) ctob(VALID_END_PFN_5);
		if(pa < ctob(VALID_END_PFN_5))
			pa += ctob(VALID_START_PFN_5);
		PHYS_TO_VM_PAGE(pa);
	}

	ATF_CHECK_EQ(true, true);
}

ATF_TP_ADD_TCS(tp)
{
	/* Fixed memory size tests. */
	ATF_TP_ADD_TC(tp, uvm_physseg_100);
	ATF_TP_ADD_TC(tp, uvm_physseg_1K);
	ATF_TP_ADD_TC(tp, uvm_physseg_10K);
	ATF_TP_ADD_TC(tp, uvm_physseg_100K);
	ATF_TP_ADD_TC(tp, uvm_physseg_1M);
	ATF_TP_ADD_TC(tp, uvm_physseg_10M);
	ATF_TP_ADD_TC(tp, uvm_physseg_100M);

#if defined(UVM_HOTPLUG)
	/* Variable memory size tests. */
	ATF_TP_ADD_TC(tp, uvm_physseg_1MB);
	ATF_TP_ADD_TC(tp, uvm_physseg_64MB);
	ATF_TP_ADD_TC(tp, uvm_physseg_128MB);
	ATF_TP_ADD_TC(tp, uvm_physseg_256MB);
#endif /* UVM_HOTPLUG */

	return atf_no_error();
}
