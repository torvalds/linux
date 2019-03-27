/* $NetBSD: t_uvm_physseg.c,v 1.2 2016/12/22 08:15:20 cherry Exp $ */

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
__RCSID("$NetBSD: t_uvm_physseg.c,v 1.2 2016/12/22 08:15:20 cherry Exp $");

/*
 * If this line is commented out tests related to uvm_physseg_get_pmseg()
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
#include <errno.h>
#include <stdbool.h>
#include <string.h> /* memset(3) et. al */
#include <stdio.h> /* printf(3) */
#include <stdlib.h> /* malloc(3) */
#include <stdarg.h>
#include <stddef.h>

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
#define VM_PHYSSEG_MAX          1
#endif

#define PAGE_SHIFT              12
#define PAGE_SIZE               (1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)
#define atop(x)         (((paddr_t)(x)) >> PAGE_SHIFT)
#define ptoa(x)         (((paddr_t)(x)) << PAGE_SHIFT)

#define	mutex_enter(l)
#define	mutex_exit(l)

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

#include <sys/kmem.h>

void *
kmem_alloc(size_t size, km_flag_t flags)
{
	return malloc(size);
}

void *
kmem_zalloc(size_t size, km_flag_t flags)
{
	void *ptr;
	ptr = malloc(size);

	memset(ptr, 0, size);

	return ptr;
}

void
kmem_free(void *mem, size_t size)
{
	free(mem);
}

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

#define SIXTYFOUR_KILO (64 * 1024)
#define ONETWENTYEIGHT_KILO (128 * 1024)
#define TWOFIFTYSIX_KILO (256 * 1024)
#define FIVEONETWO_KILO (512 * 1024)
#define ONE_MEGABYTE (1024 * 1024)
#define TWO_MEGABYTE (2 * 1024 * 1024)

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

#define VALID_START_PFN_4 atop((ONE_MEGABYTE * 3) + 1)
#define VALID_END_PFN_4 atop(ONE_MEGABYTE * 4)
#define VALID_AVAIL_START_PFN_4 atop((ONE_MEGABYTE * 3) + 1)
#define VALID_AVAIL_END_PFN_4 atop(ONE_MEGABYTE * 4)

/*
 * Total number of pages (of 4K size each) should be 256 for 1MB of memory.
 */
#define PAGE_COUNT_1M      256

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
 * Private accessor that gets the value of uvm_physseg_graph.nentries
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

#if !defined(UVM_HOTPLUG)
static void *
uvm_physseg_alloc(size_t sz)
{
	return &vm_physmem[vm_nphysseg++];
}
#endif

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


/* <---- Tests for Internal functions ----> */
#if defined(UVM_HOTPLUG)
ATF_TC(uvm_physseg_alloc_atboot_mismatch);
ATF_TC_HEAD(uvm_physseg_alloc_atboot_mismatch, tc)
{
	atf_tc_set_md_var(tc, "descr", "boot time uvm_physseg_alloc() sanity"
	    "size mismatch alloc() test.");
}

ATF_TC_BODY(uvm_physseg_alloc_atboot_mismatch, tc)
{
	uvm.page_init_done = false;

	atf_tc_expect_signal(SIGABRT, "size mismatch alloc()");

	uvm_physseg_alloc(sizeof(struct uvm_physseg) - 1);
}

ATF_TC(uvm_physseg_alloc_atboot_overrun);
ATF_TC_HEAD(uvm_physseg_alloc_atboot_overrun, tc)
{
	atf_tc_set_md_var(tc, "descr", "boot time uvm_physseg_alloc() sanity"
	    "array overrun alloc() test.");
}

ATF_TC_BODY(uvm_physseg_alloc_atboot_overrun, tc)
{
	uvm.page_init_done = false;

	atf_tc_expect_signal(SIGABRT, "array overrun alloc()");

	uvm_physseg_alloc((VM_PHYSSEG_MAX + 1) * sizeof(struct uvm_physseg));

}

ATF_TC(uvm_physseg_alloc_sanity);
ATF_TC_HEAD(uvm_physseg_alloc_sanity, tc)
{
	atf_tc_set_md_var(tc, "descr", "further uvm_physseg_alloc() sanity checks");
}

ATF_TC_BODY(uvm_physseg_alloc_sanity, tc)
{

	/* At boot time */
	uvm.page_init_done = false;

	/* Correct alloc */
	ATF_REQUIRE(uvm_physseg_alloc(VM_PHYSSEG_MAX * sizeof(struct uvm_physseg)));

	/* Retry static alloc()s as dynamic - we expect them to pass */
	uvm.page_init_done = true;
	ATF_REQUIRE(uvm_physseg_alloc(sizeof(struct uvm_physseg) - 1));
	ATF_REQUIRE(uvm_physseg_alloc(2 * VM_PHYSSEG_MAX * sizeof(struct uvm_physseg)));
}

ATF_TC(uvm_physseg_free_atboot_mismatch);
ATF_TC_HEAD(uvm_physseg_free_atboot_mismatch, tc)
{
	atf_tc_set_md_var(tc, "descr", "boot time uvm_physseg_free() sanity"
	    "size mismatch free() test.");
}

ATF_TC_BODY(uvm_physseg_free_atboot_mismatch, tc)
{
	uvm.page_init_done = false;

	atf_tc_expect_signal(SIGABRT, "size mismatch free()");

	uvm_physseg_free(&uvm_physseg[0], sizeof(struct uvm_physseg) - 1);
}

ATF_TC(uvm_physseg_free_sanity);
ATF_TC_HEAD(uvm_physseg_free_sanity, tc)
{
	atf_tc_set_md_var(tc, "descr", "further uvm_physseg_free() sanity checks");
}

ATF_TC_BODY(uvm_physseg_free_sanity, tc)
{

	/* At boot time */
	uvm.page_init_done = false;

	struct uvm_physseg *seg;

#if VM_PHYSSEG_MAX > 1
	/*
	 * Note: free()ing the entire array is considered to be an
	 * error. Thus VM_PHYSSEG_MAX - 1.
	 */

	seg = uvm_physseg_alloc((VM_PHYSSEG_MAX - 1) * sizeof(*seg));
	uvm_physseg_free(seg, (VM_PHYSSEG_MAX - 1) * sizeof(struct uvm_physseg));
#endif

	/* Retry static alloc()s as dynamic - we expect them to pass */
	uvm.page_init_done = true;

	seg = uvm_physseg_alloc(sizeof(struct uvm_physseg) - 1);
	uvm_physseg_free(seg, sizeof(struct uvm_physseg) - 1);

	seg = uvm_physseg_alloc(2 * VM_PHYSSEG_MAX * sizeof(struct uvm_physseg));

	uvm_physseg_free(seg, 2 * VM_PHYSSEG_MAX * sizeof(struct uvm_physseg));
}

#if VM_PHYSSEG_MAX > 1
ATF_TC(uvm_physseg_atboot_free_leak);
ATF_TC_HEAD(uvm_physseg_atboot_free_leak, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "does free() leak at boot ?\n"
	    "This test needs VM_PHYSSEG_MAX > 1)");
}

ATF_TC_BODY(uvm_physseg_atboot_free_leak, tc)
{

	/* At boot time */
	uvm.page_init_done = false;

	/* alloc to array size */
	struct uvm_physseg *seg;
	seg = uvm_physseg_alloc(VM_PHYSSEG_MAX * sizeof(*seg));

	uvm_physseg_free(seg, sizeof(*seg));

	atf_tc_expect_signal(SIGABRT, "array overrun on alloc() after leak");

	ATF_REQUIRE(uvm_physseg_alloc(sizeof(struct uvm_physseg)));
}
#endif /* VM_PHYSSEG_MAX */
#endif /* UVM_HOTPLUG */

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

ATF_TC(uvm_physseg_plug);
ATF_TC_HEAD(uvm_physseg_plug, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test plug functionality.");
}
/* Note: We only do the second boot time plug if VM_PHYSSEG_MAX > 1 */
ATF_TC_BODY(uvm_physseg_plug, tc)
{
	int nentries = 0; /* Count of entries via plug done so far */
	uvm_physseg_t upm1;
#if VM_PHYSSEG_MAX > 2
	uvm_physseg_t upm2;
#endif

#if VM_PHYSSEG_MAX > 1
	uvm_physseg_t upm3;
#endif
	uvm_physseg_t upm4;
	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);
	psize_t npages2 = (VALID_END_PFN_2 - VALID_START_PFN_2);
	psize_t npages3 = (VALID_END_PFN_3 - VALID_START_PFN_3);
	psize_t npages4 = (VALID_END_PFN_4 - VALID_START_PFN_4);
	struct vm_page *pgs, *slab = malloc(sizeof(struct vm_page) * (npages1
#if VM_PHYSSEG_MAX > 2
		+ npages2
#endif
		+ npages3));

	/* Fake early boot */

	setup();

	/* Vanilla plug x 2 */
	ATF_REQUIRE_EQ(uvm_physseg_plug(VALID_START_PFN_1, npages1, &upm1), true);
	ATF_REQUIRE_EQ(++nentries, uvm_physseg_get_entries());
	ATF_REQUIRE_EQ(0, uvmexp.npages);

#if VM_PHYSSEG_MAX > 2
	ATF_REQUIRE_EQ(uvm_physseg_plug(VALID_START_PFN_2, npages2, &upm2), true);
	ATF_REQUIRE_EQ(++nentries, uvm_physseg_get_entries());
	ATF_REQUIRE_EQ(0, uvmexp.npages);
#endif
	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2 + npages3);

	ATF_CHECK_EQ(npages1
#if VM_PHYSSEG_MAX > 2
	    + npages2
#endif
	    , uvmexp.npages);
#if VM_PHYSSEG_MAX > 1
	/* Scavenge plug - goes into the same slab */
	ATF_REQUIRE_EQ(uvm_physseg_plug(VALID_START_PFN_3, npages3, &upm3), true);
	ATF_REQUIRE_EQ(++nentries, uvm_physseg_get_entries());
	ATF_REQUIRE_EQ(npages1
#if VM_PHYSSEG_MAX > 2
	    + npages2
#endif
	    + npages3, uvmexp.npages);

	/* Scavenge plug should fit right in the slab */
	pgs = uvm_physseg_get_pg(upm3, 0);
	ATF_REQUIRE(pgs > slab && pgs < (slab + npages1 + npages2 + npages3));
#endif
	/* Hot plug - goes into a brand new slab */
	ATF_REQUIRE_EQ(uvm_physseg_plug(VALID_START_PFN_4, npages4, &upm4), true);
	/* The hot plug slab should have nothing to do with the original slab */
	pgs = uvm_physseg_get_pg(upm4, 0);
	ATF_REQUIRE(pgs < slab || pgs > (slab + npages1
#if VM_PHYSSEG_MAX > 2
		+ npages2
#endif
		+ npages3));

}
ATF_TC(uvm_physseg_unplug);
ATF_TC_HEAD(uvm_physseg_unplug, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test unplug functionality.");
}
ATF_TC_BODY(uvm_physseg_unplug, tc)
{
	paddr_t pa = 0;

	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);
	psize_t npages2 = (VALID_END_PFN_2 - VALID_START_PFN_2);
	psize_t npages3 = (VALID_END_PFN_3 - VALID_START_PFN_3);

	struct vm_page *slab = malloc(sizeof(struct vm_page) * (npages1 + npages2 + npages3));

	uvm_physseg_t upm;

	/* Boot time */
	setup();

	/* We start with zero segments */
	ATF_REQUIRE_EQ(true, uvm_physseg_plug(atop(0), atop(ONE_MEGABYTE), NULL));
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());
	/* Do we have an arbitrary offset in there ? */
	uvm_physseg_find(atop(TWOFIFTYSIX_KILO), &pa);
	ATF_REQUIRE_EQ(pa, atop(TWOFIFTYSIX_KILO));
	ATF_REQUIRE_EQ(0, uvmexp.npages); /* Boot time sanity */

#if VM_PHYSSEG_MAX == 1
	/*
	 * This is the curious case at boot time, of having one
	 * extent(9) static entry per segment, which means that a
	 * fragmenting unplug will fail.
	 */
	atf_tc_expect_signal(SIGABRT, "fragmenting unplug for single segment");

	/*
	 * In order to test the fragmenting cases, please set
	 * VM_PHYSSEG_MAX > 1
	 */
#endif
	/* Now let's unplug from the middle */
	ATF_REQUIRE_EQ(true, uvm_physseg_unplug(atop(TWOFIFTYSIX_KILO), atop(FIVEONETWO_KILO)));
	/* verify that a gap exists at TWOFIFTYSIX_KILO */
	pa = 0; /* reset */
	uvm_physseg_find(atop(TWOFIFTYSIX_KILO), &pa);
	ATF_REQUIRE_EQ(pa, 0);

	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2 + npages3);
	/* Account for the unplug */
	ATF_CHECK_EQ(atop(FIVEONETWO_KILO), uvmexp.npages);

	/* Original entry should fragment into two */
	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	upm = uvm_physseg_find(atop(TWOFIFTYSIX_KILO + FIVEONETWO_KILO), NULL);

	ATF_REQUIRE(uvm_physseg_valid_p(upm));

	/* Now unplug the tail fragment - should swallow the complete entry */
	ATF_REQUIRE_EQ(true, uvm_physseg_unplug(atop(TWOFIFTYSIX_KILO + FIVEONETWO_KILO), atop(TWOFIFTYSIX_KILO)));

	/* The "swallow" above should have invalidated the handle */
	ATF_REQUIRE_EQ(false, uvm_physseg_valid_p(upm));

	/* Only the first one is left now */
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Unplug from the back */
	ATF_REQUIRE_EQ(true, uvm_physseg_unplug(atop(ONETWENTYEIGHT_KILO), atop(ONETWENTYEIGHT_KILO)));
	/* Shouldn't change the number of segments */
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Unplug from the front */
	ATF_REQUIRE_EQ(true, uvm_physseg_unplug(0, atop(SIXTYFOUR_KILO)));
	/* Shouldn't change the number of segments */
	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Unplugging the final fragment should fail */
	atf_tc_expect_signal(SIGABRT, "Unplugging the last segment");
	ATF_REQUIRE_EQ(true, uvm_physseg_unplug(atop(SIXTYFOUR_KILO), atop(SIXTYFOUR_KILO)));
}


/* <---- end Tests for Internal functions ----> */

/* Tests for functions exported via uvm_physseg.h */
ATF_TC(uvm_physseg_init);
ATF_TC_HEAD(uvm_physseg_init, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_init() call\
	    initializes the vm_physmem struct which holds the rb_tree.");
}
ATF_TC_BODY(uvm_physseg_init, tc)
{
	uvm_physseg_init();

	ATF_REQUIRE_EQ(0, uvm_physseg_get_entries());
}

ATF_TC(uvm_page_physload_preload);
ATF_TC_HEAD(uvm_page_physload_preload, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_physload() \
	    call works without a panic() in a preload scenario.");
}
ATF_TC_BODY(uvm_page_physload_preload, tc)
{
	uvm_physseg_t upm;

	setup();

	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Should return a valid handle */
	ATF_REQUIRE(uvm_physseg_valid_p(upm));

	/* No pages should be allocated yet */
	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* After the first call one segment should exist */
	ATF_CHECK_EQ(1, uvm_physseg_get_entries());

	/* Insert more than one segment iff VM_PHYSSEG_MAX > 1 */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	/* Should return a valid handle */
	ATF_REQUIRE(uvm_physseg_valid_p(upm));

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* After the second call two segments should exist */
	ATF_CHECK_EQ(2, uvm_physseg_get_entries());
#endif
}

ATF_TC(uvm_page_physload_postboot);
ATF_TC_HEAD(uvm_page_physload_postboot, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_physload() \
	     panic()s in a post boot scenario.");
}
ATF_TC_BODY(uvm_page_physload_postboot, tc)
{
	uvm_physseg_t upm;

	psize_t npages1 = (VALID_END_PFN_1 - VALID_START_PFN_1);
	psize_t npages2 = (VALID_END_PFN_2 - VALID_START_PFN_2);

	struct vm_page *slab = malloc(sizeof(struct vm_page) * (npages1 + npages2));

	setup();

	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Should return a valid handle */
	ATF_REQUIRE(uvm_physseg_valid_p(upm));

	/* No pages should be allocated yet */
	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* After the first call one segment should exist */
	ATF_CHECK_EQ(1, uvm_physseg_get_entries());

	/* Post boot: Fake all segments and pages accounted for. */
	uvm_page_init_fake(slab, npages1 + npages2);

	atf_tc_expect_signal(SIGABRT,
	    "uvm_page_physload() called post boot");

	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	/* Should return a valid handle */
	ATF_REQUIRE(uvm_physseg_valid_p(upm));

	ATF_REQUIRE_EQ(npages1 + npages2, uvmexp.npages);

	/* After the second call two segments should exist */
	ATF_CHECK_EQ(2, uvm_physseg_get_entries());
}

ATF_TC(uvm_physseg_handle_immutable);
ATF_TC_HEAD(uvm_physseg_handle_immutable, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the uvm_physseg_t handle is \
	    immutable.");
}
ATF_TC_BODY(uvm_physseg_handle_immutable, tc)
{
	uvm_physseg_t upm;

	/* We insert the segments in out of order */

	setup();

	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID_EMPTY, uvm_physseg_get_prev(upm));

	/* Insert more than one segment iff VM_PHYSSEG_MAX > 1 */
#if VM_PHYSSEG_MAX > 1
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	/* Fetch Previous, we inserted a lower value */
	upm = uvm_physseg_get_prev(upm);

#if !defined(UVM_HOTPLUG)
	/*
	 * This test is going to fail for the Array Implementation but is
	 * expected to pass in the RB Tree implementation.
	 */
	/* Failure can be expected iff there are more than one handles */
	atf_tc_expect_fail("Mutable handle in static array impl.");
#endif
	ATF_CHECK(UVM_PHYSSEG_TYPE_INVALID_EMPTY != upm);
	ATF_CHECK_EQ(VALID_START_PFN_1, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_1, uvm_physseg_get_end(upm));
#endif
}

ATF_TC(uvm_physseg_seg_chomp_slab);
ATF_TC_HEAD(uvm_physseg_seg_chomp_slab, tc)
{
	atf_tc_set_md_var(tc, "descr", "The slab import code.()");

}
ATF_TC_BODY(uvm_physseg_seg_chomp_slab, tc)
{
	int err;
	size_t i;
	struct uvm_physseg *seg;
	struct vm_page *slab, *pgs;
	const size_t npages = UVM_PHYSSEG_BOOT_UNPLUG_MAX; /* Number of pages */

	setup();

	/* This is boot time */
	slab = malloc(sizeof(struct vm_page) * npages * 2);

	seg = uvm_physseg_alloc(sizeof(struct uvm_physseg));

	uvm_physseg_seg_chomp_slab(PHYSSEG_NODE_TO_HANDLE(seg), slab, npages * 2);

	/* Should be able to allocate two 128 * sizeof(*slab) */
	ATF_REQUIRE_EQ(0, extent_alloc(seg->ext, sizeof(*slab), 1, 0, EX_BOUNDZERO, (void *)&pgs));
	err = extent_free(seg->ext, (u_long) pgs, sizeof(*slab), EX_BOUNDZERO);

#if VM_PHYSSEG_MAX == 1
	/*
	 * free() needs an extra region descriptor, but we only have
	 * one! The classic alloc() at free() problem
	 */

	ATF_REQUIRE_EQ(ENOMEM, err);
#else
	/* Try alloc/free at static time */
	for (i = 0; i < npages; i++) {
		ATF_REQUIRE_EQ(0, extent_alloc(seg->ext, sizeof(*slab), 1, 0, EX_BOUNDZERO, (void *)&pgs));
		err = extent_free(seg->ext, (u_long) pgs, sizeof(*slab), EX_BOUNDZERO);
		ATF_REQUIRE_EQ(0, err);
	}
#endif

	/* Now setup post boot */
	uvm.page_init_done = true;

	uvm_physseg_seg_chomp_slab(PHYSSEG_NODE_TO_HANDLE(seg), slab, npages * 2);

	/* Try alloc/free after uvm_page.c:uvm_page_init() as well */
	for (i = 0; i < npages; i++) {
		ATF_REQUIRE_EQ(0, extent_alloc(seg->ext, sizeof(*slab), 1, 0, EX_BOUNDZERO, (void *)&pgs));
		err = extent_free(seg->ext, (u_long) pgs, sizeof(*slab), EX_BOUNDZERO);
		ATF_REQUIRE_EQ(0, err);
	}

}

ATF_TC(uvm_physseg_alloc_from_slab);
ATF_TC_HEAD(uvm_physseg_alloc_from_slab, tc)
{
	atf_tc_set_md_var(tc, "descr", "The slab alloc code.()");

}
ATF_TC_BODY(uvm_physseg_alloc_from_slab, tc)
{
	struct uvm_physseg *seg;
	struct vm_page *slab, *pgs;
	const size_t npages = UVM_PHYSSEG_BOOT_UNPLUG_MAX; /* Number of pages */

	setup();

	/* This is boot time */
	slab = malloc(sizeof(struct vm_page) * npages * 2);

	seg = uvm_physseg_alloc(sizeof(struct uvm_physseg));

	uvm_physseg_seg_chomp_slab(PHYSSEG_NODE_TO_HANDLE(seg), slab, npages * 2);

	pgs = uvm_physseg_seg_alloc_from_slab(PHYSSEG_NODE_TO_HANDLE(seg), npages);

	ATF_REQUIRE(pgs != NULL);

	/* Now setup post boot */
	uvm.page_init_done = true;

#if VM_PHYSSEG_MAX > 1
	pgs = uvm_physseg_seg_alloc_from_slab(PHYSSEG_NODE_TO_HANDLE(seg), npages);
	ATF_REQUIRE(pgs != NULL);
#endif
	atf_tc_expect_fail("alloc beyond extent");

	pgs = uvm_physseg_seg_alloc_from_slab(PHYSSEG_NODE_TO_HANDLE(seg), npages);
	ATF_REQUIRE(pgs != NULL);
}

ATF_TC(uvm_physseg_init_seg);
ATF_TC_HEAD(uvm_physseg_init_seg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if uvm_physseg_init_seg adds pages to"
	    "uvmexp.npages");
}
ATF_TC_BODY(uvm_physseg_init_seg, tc)
{
	struct uvm_physseg *seg;
	struct vm_page *slab, *pgs;
	const size_t npages = UVM_PHYSSEG_BOOT_UNPLUG_MAX; /* Number of pages */

	setup();

	/* This is boot time */
	slab = malloc(sizeof(struct vm_page) * npages * 2);

	seg = uvm_physseg_alloc(sizeof(struct uvm_physseg));

	uvm_physseg_seg_chomp_slab(PHYSSEG_NODE_TO_HANDLE(seg), slab, npages * 2);

	pgs = uvm_physseg_seg_alloc_from_slab(PHYSSEG_NODE_TO_HANDLE(seg), npages);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	seg->start = 0;
	seg->end = npages;

	seg->avail_start = 0;
	seg->avail_end = npages;

	uvm_physseg_init_seg(PHYSSEG_NODE_TO_HANDLE(seg), pgs);

	ATF_REQUIRE_EQ(npages, uvmexp.npages);
}

#if 0
ATF_TC(uvm_physseg_init_seg);
ATF_TC_HEAD(uvm_physseg_init_seg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_physload() \
	    call works without a panic() after Segment is inited.");
}
ATF_TC_BODY(uvm_physseg_init_seg, tc)
{
	uvm_physseg_t upm;
	psize_t npages = (VALID_END_PFN_1 - VALID_START_PFN_1);
	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(0, uvmexp.npages);

	/*
	 * Boot time physplug needs explicit external init,
	 * Duplicate what uvm_page.c:uvm_page_init() does.
	 * Note: not everything uvm_page_init() does gets done here.
	 * Read the source.
	 */
	/* suck in backing slab, initialise extent. */
	uvm_physseg_seg_chomp_slab(upm, pgs, npages);

	/*
	 * Actual pgs[] allocation, from extent.
	 */
	uvm_physseg_alloc_from_slab(upm, npages);

	/* Now we initialize the segment */
	uvm_physseg_init_seg(upm, pgs);

	/* Done with boot simulation */
	extent_init();
	uvm.page_init_done = true;

	/* We have total memory of 1MB */
	ATF_CHECK_EQ(PAGE_COUNT_1M, uvmexp.npages);

	upm =uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);
	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	/* We added another 1MB so PAGE_COUNT_1M + PAGE_COUNT_1M */
	ATF_CHECK_EQ(PAGE_COUNT_1M + PAGE_COUNT_1M, uvmexp.npages);

}
#endif

ATF_TC(uvm_physseg_get_start);
ATF_TC_HEAD(uvm_physseg_get_start, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the start PFN is returned \
	    correctly from a segment created via uvm_page_physload().");
}
ATF_TC_BODY(uvm_physseg_get_start, tc)
{
	uvm_physseg_t upm;

	/* Fake early boot */
	setup();

	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(VALID_START_PFN_1, uvm_physseg_get_start(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
#endif
}

ATF_TC(uvm_physseg_get_start_invalid);
ATF_TC_HEAD(uvm_physseg_get_start_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the invalid / error conditions \
	    correctly when uvm_physseg_get_start() is called with invalid \
	    parameter values.");
}
ATF_TC_BODY(uvm_physseg_get_start_invalid, tc)
{
	/* Check for pgs == NULL */
	setup();
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Force other check conditions */
	uvm.page_init_done = true;

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(true, uvm.page_init_done);

	/* Invalid uvm_physseg_t */
	ATF_CHECK_EQ((paddr_t) -1,
	    uvm_physseg_get_start(UVM_PHYSSEG_TYPE_INVALID));
}

ATF_TC(uvm_physseg_get_end);
ATF_TC_HEAD(uvm_physseg_get_end, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the end PFN is returned \
	    correctly from a segment created via uvm_page_physload().");
}
ATF_TC_BODY(uvm_physseg_get_end, tc)
{
	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(VALID_END_PFN_1, uvm_physseg_get_end(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
#endif
}

ATF_TC(uvm_physseg_get_end_invalid);
ATF_TC_HEAD(uvm_physseg_get_end_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the invalid / error conditions \
	    correctly when uvm_physseg_get_end() is called with invalid \
	    parameter values.");
}
ATF_TC_BODY(uvm_physseg_get_end_invalid, tc)
{
	/* Check for pgs == NULL */
	setup();
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Force other check conditions */
	uvm.page_init_done = true;

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(true, uvm.page_init_done);

	/* Invalid uvm_physseg_t */
	ATF_CHECK_EQ((paddr_t) -1,
	    uvm_physseg_get_end(UVM_PHYSSEG_TYPE_INVALID));
}

ATF_TC(uvm_physseg_get_avail_start);
ATF_TC_HEAD(uvm_physseg_get_avail_start, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the avail_start PFN is \
	    returned correctly from a segment created via uvm_page_physload().");
}
ATF_TC_BODY(uvm_physseg_get_avail_start, tc)
{
	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_1, uvm_physseg_get_avail_start(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2, uvm_physseg_get_avail_start(upm));
#endif
}

ATF_TC(uvm_physseg_get_avail_start_invalid);
ATF_TC_HEAD(uvm_physseg_get_avail_start_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the invalid / error conditions \
	    correctly when uvm_physseg_get_avail_start() is called with invalid\
	    parameter values.");
}
ATF_TC_BODY(uvm_physseg_get_avail_start_invalid, tc)
{
	/* Check for pgs == NULL */
	setup();
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Force other check conditions */
	uvm.page_init_done = true;

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(true, uvm.page_init_done);

	/* Invalid uvm_physseg_t */
	ATF_CHECK_EQ((paddr_t) -1,
	    uvm_physseg_get_avail_start(UVM_PHYSSEG_TYPE_INVALID));
}

ATF_TC(uvm_physseg_get_avail_end);
ATF_TC_HEAD(uvm_physseg_get_avail_end, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the avail_end PFN is \
	    returned correctly from a segment created via uvm_page_physload().");
}
ATF_TC_BODY(uvm_physseg_get_avail_end, tc)
{
	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_1, uvm_physseg_get_avail_end(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2, uvm_physseg_get_avail_end(upm));
#endif
}

ATF_TC(uvm_physseg_get_avail_end_invalid);
ATF_TC_HEAD(uvm_physseg_get_avail_end_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the invalid / error conditions \
	    correctly when uvm_physseg_get_avail_end() is called with invalid\
	    parameter values.");
}
ATF_TC_BODY(uvm_physseg_get_avail_end_invalid, tc)
{
	/* Check for pgs == NULL */
	setup();
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Force other check conditions */
	uvm.page_init_done = true;

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(true, uvm.page_init_done);

	/* Invalid uvm_physseg_t */
	ATF_CHECK_EQ((paddr_t) -1,
	    uvm_physseg_get_avail_end(UVM_PHYSSEG_TYPE_INVALID));
}

ATF_TC(uvm_physseg_get_next);
ATF_TC_HEAD(uvm_physseg_get_next, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the pointer values for next \
	    segment using the uvm_physseg_get_next() call.");
}
ATF_TC_BODY(uvm_physseg_get_next, tc)
{
	uvm_physseg_t upm;
#if VM_PHYSSEG_MAX > 1
	uvm_physseg_t upm_next;
#endif

	/* We insert the segments in ascending order */

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID_OVERFLOW,
	    uvm_physseg_get_next(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm_next = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	upm = uvm_physseg_get_next(upm); /* Fetch Next */

	ATF_CHECK_EQ(upm_next, upm);
	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
#endif

	/* This test will be triggered only if there are 3 or more segments. */
#if VM_PHYSSEG_MAX > 2
	upm_next = uvm_page_physload(VALID_START_PFN_3, VALID_END_PFN_3,
	    VALID_AVAIL_START_PFN_3, VALID_AVAIL_END_PFN_3, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(3, uvm_physseg_get_entries());

	upm = uvm_physseg_get_next(upm); /* Fetch Next */

	ATF_CHECK_EQ(upm_next, upm);
	ATF_CHECK_EQ(VALID_START_PFN_3, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_3, uvm_physseg_get_end(upm));
#endif
}

ATF_TC(uvm_physseg_get_next_invalid);
ATF_TC_HEAD(uvm_physseg_get_next_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the invalid / error conditions \
	    correctly when uvm_physseg_get_next() is called with invalid \
	    parameter values.");
}
ATF_TC_BODY(uvm_physseg_get_next_invalid, tc)
{
	uvm_physseg_t upm = UVM_PHYSSEG_TYPE_INVALID;

	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID, uvm_physseg_get_next(upm));
}

ATF_TC(uvm_physseg_get_prev);
ATF_TC_HEAD(uvm_physseg_get_prev, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the pointer values for previous \
	    segment using the uvm_physseg_get_prev() call.");
}
ATF_TC_BODY(uvm_physseg_get_prev, tc)
{
#if VM_PHYSSEG_MAX > 1
	uvm_physseg_t upm;
#endif
	uvm_physseg_t upm_prev;


	setup();
	upm_prev = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID_EMPTY,
	    uvm_physseg_get_prev(upm_prev));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	/* Fetch Previous, we inserted a lower value */
	upm = uvm_physseg_get_prev(upm);

	ATF_CHECK_EQ(upm_prev, upm);
	ATF_CHECK_EQ(VALID_START_PFN_1, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_1, uvm_physseg_get_end(upm));
#endif

	/* This test will be triggered only if there are 3 or more segments. */
#if VM_PHYSSEG_MAX > 2
	uvm_page_physload(VALID_START_PFN_3, VALID_END_PFN_3,
	    VALID_AVAIL_START_PFN_3, VALID_AVAIL_END_PFN_3, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(3, uvm_physseg_get_entries());

	/*
	 * This will return a UVM_PHYSSEG_TYPE_INVALID_EMPTY we are at the
	 * lowest
	 */
	upm = uvm_physseg_get_prev(upm);

	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID_EMPTY, upm);
#endif
}

ATF_TC(uvm_physseg_get_prev_invalid);
ATF_TC_HEAD(uvm_physseg_get_prev_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the invalid / error conditions \
	    correctly when uvm_physseg_get_prev() is called with invalid \
	    parameter values.");
}
ATF_TC_BODY(uvm_physseg_get_prev_invalid, tc)
{
	uvm_physseg_t upm = UVM_PHYSSEG_TYPE_INVALID;

	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID, uvm_physseg_get_prev(upm));
}

ATF_TC(uvm_physseg_get_first);
ATF_TC_HEAD(uvm_physseg_get_first, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the pointer values for first \
	    segment (lowest node) using the uvm_physseg_get_first() call.");
}
ATF_TC_BODY(uvm_physseg_get_first, tc)
{
	uvm_physseg_t upm = UVM_PHYSSEG_TYPE_INVALID_EMPTY;
	uvm_physseg_t upm_first;

	/* Fake early boot */
	setup();

	/* No nodes exist */
	ATF_CHECK_EQ(upm, uvm_physseg_get_first());

	upm_first = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Pointer to first should be the least valued node */
	upm = uvm_physseg_get_first();
	ATF_CHECK_EQ(upm_first, upm);
	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2, uvm_physseg_get_avail_end(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	/* Insert a node of lesser value */
	upm_first = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_CHECK_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	/* Pointer to first should be the least valued node */
	upm = uvm_physseg_get_first();
	ATF_CHECK_EQ(upm_first, upm);
	ATF_CHECK_EQ(VALID_START_PFN_1, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_1, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_1, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_1, uvm_physseg_get_avail_end(upm));
#endif

	/* This test will be triggered only if there are 3 or more segments. */
#if VM_PHYSSEG_MAX > 2
	/* Insert a node of higher value */
	upm_first =uvm_page_physload(VALID_START_PFN_3, VALID_END_PFN_3,
	    VALID_AVAIL_START_PFN_3, VALID_AVAIL_END_PFN_3, VM_FREELIST_DEFAULT);

	ATF_CHECK_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(3, uvm_physseg_get_entries());

	/* Pointer to first should be the least valued node */
	upm = uvm_physseg_get_first();
	ATF_CHECK(upm_first != upm);
	ATF_CHECK_EQ(VALID_START_PFN_1, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_1, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_1, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_1, uvm_physseg_get_avail_end(upm));
#endif
}

ATF_TC(uvm_physseg_get_last);
ATF_TC_HEAD(uvm_physseg_get_last, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the pointer values for last \
	    segment using the uvm_physseg_get_last() call.");
}
ATF_TC_BODY(uvm_physseg_get_last, tc)
{
	uvm_physseg_t upm = UVM_PHYSSEG_TYPE_INVALID_EMPTY;
	uvm_physseg_t upm_last;

	setup();

	/* No nodes exist */
	ATF_CHECK_EQ(upm, uvm_physseg_get_last());

	upm_last = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	/* Pointer to last should be the most valued node */
	upm = uvm_physseg_get_last();
	ATF_CHECK_EQ(upm_last, upm);
	ATF_CHECK_EQ(VALID_START_PFN_1, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_1, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_1, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_1, uvm_physseg_get_avail_end(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	/* Insert node of greater value */
	upm_last = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	/* Pointer to last should be the most valued node */
	upm = uvm_physseg_get_last();
	ATF_CHECK_EQ(upm_last, upm);
	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2, uvm_physseg_get_avail_end(upm));
#endif

	/* This test will be triggered only if there are 3 or more segments. */
#if VM_PHYSSEG_MAX > 2
	/* Insert node of greater value */
	upm_last = uvm_page_physload(VALID_START_PFN_3, VALID_END_PFN_3,
	    VALID_AVAIL_START_PFN_3, VALID_AVAIL_END_PFN_3, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(3, uvm_physseg_get_entries());

	/* Pointer to last should be the most valued node */
	upm = uvm_physseg_get_last();
	ATF_CHECK_EQ(upm_last, upm);
	ATF_CHECK_EQ(VALID_START_PFN_3, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_3, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_3, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_3, uvm_physseg_get_avail_end(upm));
#endif
}

ATF_TC(uvm_physseg_valid);
ATF_TC_HEAD(uvm_physseg_valid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the pointer value for current \
	    segment is valid using the uvm_physseg_valid_p() call.");
}
ATF_TC_BODY(uvm_physseg_valid, tc)
{
	psize_t npages = (VALID_END_PFN_1 - VALID_START_PFN_1);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	uvm_physseg_init_seg(upm, pgs);

	ATF_REQUIRE_EQ(PAGE_COUNT_1M, uvmexp.npages);

	ATF_CHECK_EQ(true, uvm_physseg_valid_p(upm));
}

ATF_TC(uvm_physseg_valid_invalid);
ATF_TC_HEAD(uvm_physseg_valid_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the pointer value for current \
	    segment is invalid using the uvm_physseg_valid_p() call.");
}
ATF_TC_BODY(uvm_physseg_valid_invalid, tc)
{
	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Force other check conditions */
	uvm.page_init_done = true;

	ATF_REQUIRE_EQ(true, uvm.page_init_done);

	/* Invalid uvm_physseg_t */
	ATF_CHECK_EQ(false, uvm_physseg_valid_p(UVM_PHYSSEG_TYPE_INVALID));

	/*
	 * Without any pages initialized for segment, it is considered
	 * invalid
	 */
	ATF_CHECK_EQ(false, uvm_physseg_valid_p(upm));
}

ATF_TC(uvm_physseg_get_highest);
ATF_TC_HEAD(uvm_physseg_get_highest, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned PFN matches  \
	    the highest PFN in use by the system.");
}
ATF_TC_BODY(uvm_physseg_get_highest, tc)
{
	setup();
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Only one segment so highest is the current */
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_1 - 1, uvm_physseg_get_highest_frame());

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	uvm_page_physload(VALID_START_PFN_3, VALID_END_PFN_3,
	    VALID_AVAIL_START_PFN_3, VALID_AVAIL_END_PFN_3, VM_FREELIST_DEFAULT);

	/* PFN_3 > PFN_1 */
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_3 - 1, uvm_physseg_get_highest_frame());
#endif

	/* This test will be triggered only if there are 3 or more segments. */
#if VM_PHYSSEG_MAX > 2
	uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	/* PFN_3 > PFN_2 */
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_3 - 1, uvm_physseg_get_highest_frame());
#endif
}

ATF_TC(uvm_physseg_get_free_list);
ATF_TC_HEAD(uvm_physseg_get_free_list, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned Free List type \
	    of a segment matches the one returned from \
	    uvm_physseg_get_free_list() call.");
}
ATF_TC_BODY(uvm_physseg_get_free_list, tc)
{
	uvm_physseg_t upm;

	/* Fake early boot */
	setup();

	/* Insertions are made in ascending order */
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_CHECK_EQ(VM_FREELIST_DEFAULT, uvm_physseg_get_free_list(upm));

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_FIRST16);

	ATF_CHECK_EQ(VM_FREELIST_FIRST16, uvm_physseg_get_free_list(upm));
#endif

	/* This test will be triggered only if there are 3 or more segments. */
#if VM_PHYSSEG_MAX > 2
	upm = uvm_page_physload(VALID_START_PFN_3, VALID_END_PFN_3,
	    VALID_AVAIL_START_PFN_3, VALID_AVAIL_END_PFN_3, VM_FREELIST_FIRST1G);

	ATF_CHECK_EQ(VM_FREELIST_FIRST1G, uvm_physseg_get_free_list(upm));
#endif
}

ATF_TC(uvm_physseg_get_start_hint);
ATF_TC_HEAD(uvm_physseg_get_start_hint, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned start_hint value \
	    of a segment matches the one returned from \
	    uvm_physseg_get_start_hint() call.");
}
ATF_TC_BODY(uvm_physseg_get_start_hint, tc)
{
	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Will be Zero since no specific value is set during init */
	ATF_CHECK_EQ(0, uvm_physseg_get_start_hint(upm));
}

ATF_TC(uvm_physseg_set_start_hint);
ATF_TC_HEAD(uvm_physseg_set_start_hint, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned start_hint value \
	    of a segment matches the one set by the \
	    uvm_physseg_set_start_hint() call.");
}
ATF_TC_BODY(uvm_physseg_set_start_hint, tc)
{
	psize_t npages = (VALID_END_PFN_1 - VALID_START_PFN_1);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	uvm_physseg_init_seg(upm, pgs);

	ATF_CHECK_EQ(true, uvm_physseg_set_start_hint(upm, atop(128)));

	/* Will be atop(128) since no specific value is set above */
	ATF_CHECK_EQ(atop(128), uvm_physseg_get_start_hint(upm));
}

ATF_TC(uvm_physseg_set_start_hint_invalid);
ATF_TC_HEAD(uvm_physseg_set_start_hint_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned value is false \
	    when an invalid segment matches the one trying to set by the \
	    uvm_physseg_set_start_hint() call.");
}
ATF_TC_BODY(uvm_physseg_set_start_hint_invalid, tc)
{
	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	/* Force other check conditions */
	uvm.page_init_done = true;

	ATF_REQUIRE_EQ(true, uvm.page_init_done);

	ATF_CHECK_EQ(false, uvm_physseg_set_start_hint(upm, atop(128)));

	/*
	 * Will be Zero since no specific value is set after the init
	 * due to failure
	 */
	atf_tc_expect_signal(SIGABRT, "invalid uvm_physseg_t handle");

	ATF_CHECK_EQ(0, uvm_physseg_get_start_hint(upm));
}

ATF_TC(uvm_physseg_get_pg);
ATF_TC_HEAD(uvm_physseg_get_pg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned vm_page struct \
	    is correct when fetched by uvm_physseg_get_pg() call.");
}
ATF_TC_BODY(uvm_physseg_get_pg, tc)
{
	psize_t npages = (VALID_END_PFN_1 - VALID_START_PFN_1);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	struct vm_page *extracted_pg = NULL;

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* Now we initialize the segment */
	uvm_physseg_init_seg(upm, pgs);

	ATF_REQUIRE_EQ(PAGE_COUNT_1M, uvmexp.npages);

	ATF_REQUIRE_EQ(NULL, extracted_pg);

	/* Try fetching the 5th Page in the Segment */
	extracted_pg = uvm_physseg_get_pg(upm, 5);

	/* Values of phys_addr is n * PAGE_SIZE where n is the page number */
	ATF_CHECK_EQ(5 * PAGE_SIZE, extracted_pg->phys_addr);

	/* Try fetching the 113th Page in the Segment */
	extracted_pg = uvm_physseg_get_pg(upm, 113);

	ATF_CHECK_EQ(113 * PAGE_SIZE, extracted_pg->phys_addr);
}

#ifdef __HAVE_PMAP_PHYSSEG
ATF_TC(uvm_physseg_get_pmseg);
ATF_TC_HEAD(uvm_physseg_get_pmseg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned pmap_physseg \
	    struct is correct when fetched by uvm_physseg_get_pmseg() call.");
}
ATF_TC_BODY(uvm_physseg_get_pmseg, tc)
{
	psize_t npages = (VALID_END_PFN_1 - VALID_START_PFN_1);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	struct pmap_physseg pmseg = { true };

	struct pmap_physseg *extracted_pmseg = NULL;

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* Now we initialize the segment */
	uvm_physseg_init_seg(upm, pgs);

	ATF_REQUIRE_EQ(PAGE_COUNT_1M, uvmexp.npages);

	ATF_REQUIRE_EQ(NULL, extracted_pmseg);

	ATF_REQUIRE_EQ(true, pmseg.dummy_variable);

	/* Extract the current pmseg */
	extracted_pmseg = uvm_physseg_get_pmseg(upm);

	/*
	 * We can only check if it is not NULL
	 * We do not know the value it contains
	 */
	ATF_CHECK(NULL != extracted_pmseg);

	extracted_pmseg->dummy_variable = pmseg.dummy_variable;

	/* Invert value to ensure test integrity */
	pmseg.dummy_variable = false;

	ATF_REQUIRE_EQ(false, pmseg.dummy_variable);

	extracted_pmseg = uvm_physseg_get_pmseg(upm);

	ATF_CHECK(NULL != extracted_pmseg);

	ATF_CHECK_EQ(true, extracted_pmseg->dummy_variable);
}
#endif

ATF_TC(vm_physseg_find);
ATF_TC_HEAD(vm_physseg_find, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned segment number \
	    is correct when an PFN is passed into uvm_physseg_find() call. \
	    In addition	to this the offset of the PFN from the start of \
	    segment is also set if the parameter is passed in as not NULL.");
}
ATF_TC_BODY(vm_physseg_find, tc)
{
	psize_t offset = (psize_t) -1;

	uvm_physseg_t upm_first, result;
#if VM_PHYSSEG_MAX > 1
	uvm_physseg_t upm_second;
#endif

	setup();

	upm_first = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	upm_second = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);
#endif

	/* Under ONE_MEGABYTE is segment upm_first */
	result = uvm_physseg_find(atop(ONE_MEGABYTE - 1024), NULL);
	ATF_CHECK_EQ(upm_first, result);
	ATF_CHECK_EQ(uvm_physseg_get_start(upm_first),
	    uvm_physseg_get_start(result));
	ATF_CHECK_EQ(uvm_physseg_get_end(upm_first),
	    uvm_physseg_get_end(result));
	ATF_CHECK_EQ(uvm_physseg_get_avail_start(upm_first),
	    uvm_physseg_get_avail_start(result));
	ATF_CHECK_EQ(uvm_physseg_get_avail_end(upm_first),
	    uvm_physseg_get_avail_end(result));

	ATF_REQUIRE_EQ((psize_t) -1, offset);

	/* This test will be triggered only if there are 2 or more segments. */
#if VM_PHYSSEG_MAX > 1
	/* Over ONE_MEGABYTE is segment upm_second */
	result = uvm_physseg_find(atop(ONE_MEGABYTE + 8192), &offset);
	ATF_CHECK_EQ(upm_second, result);
	ATF_CHECK_EQ(uvm_physseg_get_start(upm_second),
	    uvm_physseg_get_start(result));
	ATF_CHECK_EQ(uvm_physseg_get_end(upm_second),
	    uvm_physseg_get_end(result));
	ATF_CHECK_EQ(uvm_physseg_get_avail_start(upm_second),
	    uvm_physseg_get_avail_start(result));
	ATF_CHECK_EQ(uvm_physseg_get_avail_end(upm_second),
	    uvm_physseg_get_avail_end(result));

	/* Offset is calculated based on PAGE_SIZE */
	/* atop(ONE_MEGABYTE + (2 * PAGE_SIZE)) - VALID_START_PFN1  = 2 */
	ATF_CHECK_EQ(2, offset);
#else
	/* Under ONE_MEGABYTE is segment upm_first */
	result = uvm_physseg_find(atop(ONE_MEGABYTE - 12288), &offset);
	ATF_CHECK_EQ(upm_first, result);
	ATF_CHECK_EQ(uvm_physseg_get_start(upm_first),
	    uvm_physseg_get_start(result));
	ATF_CHECK_EQ(uvm_physseg_get_end(upm_first),
	    uvm_physseg_get_end(result));
	ATF_CHECK_EQ(uvm_physseg_get_avail_start(upm_first),
	    uvm_physseg_get_avail_start(result));
	ATF_CHECK_EQ(uvm_physseg_get_avail_end(upm_first),
	    uvm_physseg_get_avail_end(result));

	/* Offset is calculated based on PAGE_SIZE */
	/* atop(ONE_MEGABYTE - (3 * PAGE_SIZE)) - VALID_START_PFN1  = 253 */
	ATF_CHECK_EQ(253, offset);
#endif
}

ATF_TC(vm_physseg_find_invalid);
ATF_TC_HEAD(vm_physseg_find_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the returned segment number \
	    is (paddr_t) -1  when a non existant PFN is passed into \
	    uvm_physseg_find() call.");
}
ATF_TC_BODY(vm_physseg_find_invalid, tc)
{
	psize_t offset = (psize_t) -1;

	setup();
	uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* No segments over 3 MB exists at the moment */
	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID,
	    uvm_physseg_find(atop(ONE_MEGABYTE * 3), NULL));

	ATF_REQUIRE_EQ((psize_t) -1, offset);

	/* No segments over 3 MB exists at the moment */
	ATF_CHECK_EQ(UVM_PHYSSEG_TYPE_INVALID,
	    uvm_physseg_find(atop(ONE_MEGABYTE * 3), &offset));

	ATF_CHECK_EQ((psize_t) -1, offset);
}

ATF_TC(uvm_page_physunload_start);
ATF_TC_HEAD(uvm_page_physunload_start, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_physunload()\
	    call works without a panic(). Unloads from Start of the segment.");
}
ATF_TC_BODY(uvm_page_physunload_start, tc)
{
	/*
	 * Would uvmexp.npages reduce everytime an uvm_page_physunload is called?
	 */
	psize_t npages = (VALID_END_PFN_2 - VALID_START_PFN_2);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	uvm_physseg_init_seg(upm, pgs);

	ATF_CHECK_EQ(true, uvm_page_physunload(upm, VM_FREELIST_DEFAULT, &p));

	/*
	 * When called for first time, uvm_page_physload() removes the first PFN
	 *
	 * New avail start will be VALID_AVAIL_START_PFN_2 + 1
	 */
	ATF_CHECK_EQ(VALID_START_PFN_2, atop(p));

	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2 + 1,
	    uvm_physseg_get_avail_start(upm));

	ATF_CHECK_EQ(VALID_START_PFN_2 + 1, uvm_physseg_get_start(upm));

	/* Rest of the stuff should remain the same */
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2, uvm_physseg_get_avail_end(upm));
}

ATF_TC(uvm_page_physunload_end);
ATF_TC_HEAD(uvm_page_physunload_end, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_physunload()\
	    call works without a panic(). Unloads from End of the segment.");
}
ATF_TC_BODY(uvm_page_physunload_end, tc)
{
	/*
	 * Would uvmexp.npages reduce everytime an uvm_page_physunload is called?
	 */
	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();
	/* Note: start != avail_start to remove from end. */
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2 + 1, VALID_AVAIL_END_PFN_2,
	    VM_FREELIST_DEFAULT);

	p = 0;

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE(
		uvm_physseg_get_avail_start(upm) != uvm_physseg_get_start(upm));

	ATF_CHECK_EQ(true, uvm_page_physunload(upm, VM_FREELIST_DEFAULT, &p));

	/*
	 * Remember if X is the upper limit the actual valid pointer is X - 1
	 *
	 * For example if 256 is the upper limit for 1MB memory, last valid
	 * pointer is 256 - 1 = 255
	 */

	ATF_CHECK_EQ(VALID_END_PFN_2 - 1, atop(p));

	/*
	 * When called for second time, uvm_page_physload() removes the last PFN
	 *
	 * New avail end will be VALID_AVAIL_END_PFN_2 - 1
	 * New end will be VALID_AVAIL_PFN_2 - 1
	 */

	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2 - 1, uvm_physseg_get_avail_end(upm));

	ATF_CHECK_EQ(VALID_END_PFN_2 - 1, uvm_physseg_get_end(upm));

	/* Rest of the stuff should remain the same */
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2 + 1,
	    uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
}

ATF_TC(uvm_page_physunload_none);
ATF_TC_HEAD(uvm_page_physunload_none, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic uvm_page_physunload()\
	    call works without a panic(). Does not unload from start or end \
	    because of non-aligned start / avail_start and end / avail_end \
	    respectively.");
}
ATF_TC_BODY(uvm_page_physunload_none, tc)
{
	psize_t npages = (VALID_END_PFN_2 - VALID_START_PFN_2);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();
	/*
	 * Note: start != avail_start and end != avail_end.
	 *
	 * This prevents any unload from occuring.
	 */
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2 + 1, VALID_AVAIL_END_PFN_2 - 1,
	    VM_FREELIST_DEFAULT);

	p = 0;

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_REQUIRE(
		uvm_physseg_get_avail_start(upm) != uvm_physseg_get_start(upm));

	uvm_physseg_init_seg(upm, pgs);

	ATF_CHECK_EQ(false, uvm_page_physunload(upm, VM_FREELIST_DEFAULT, &p));

	/* uvm_page_physload() will no longer unload memory */
	ATF_CHECK_EQ(0, p);

	/* Rest of the stuff should remain the same */
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2 + 1,
	    uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2 - 1,
	    uvm_physseg_get_avail_end(upm));
	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
}

ATF_TC(uvm_page_physunload_delete_start);
ATF_TC_HEAD(uvm_page_physunload_delete_start, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the  uvm_page_physunload() \
	    works when the segment gets small enough to be deleted scenario. \
	    NOTE: This one works deletes from start.");
}
ATF_TC_BODY(uvm_page_physunload_delete_start, tc)
{
	/*
	 * Would uvmexp.npages reduce everytime an uvm_page_physunload is called?
	 */
	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();

	/*
	 * Setup the Nuke from Starting point
	 */

	upm = uvm_page_physload(VALID_END_PFN_1 - 1, VALID_END_PFN_1,
	    VALID_AVAIL_END_PFN_1 - 1, VALID_AVAIL_END_PFN_1,
	    VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* Insert more than one segment iff VM_PHYSSEG_MAX > 1 */
#if VM_PHYSSEG_MAX > 1
	uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());
#endif

#if VM_PHYSSEG_MAX == 1
	atf_tc_expect_signal(SIGABRT,
	    "cannot uvm_page_physunload() the last segment");
#endif

	ATF_CHECK_EQ(true, uvm_page_physunload(upm, VM_FREELIST_DEFAULT, &p));

	ATF_CHECK_EQ(VALID_END_PFN_1 - 1, atop(p));

	ATF_CHECK_EQ(1, uvm_physseg_get_entries());

	/* The only node now is the one we inserted second. */
	upm = uvm_physseg_get_first();

	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2, uvm_physseg_get_avail_end(upm));
}

ATF_TC(uvm_page_physunload_delete_end);
ATF_TC_HEAD(uvm_page_physunload_delete_end, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the  uvm_page_physunload() \
	    works when the segment gets small enough to be deleted scenario. \
	    NOTE: This one works deletes from end.");
}
ATF_TC_BODY(uvm_page_physunload_delete_end, tc)
{
	/*
	 * Would uvmexp.npages reduce everytime an uvm_page_physunload is called?
	 */

	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();

	/*
	 * Setup the Nuke from Ending point
	 */

	upm = uvm_page_physload(VALID_START_PFN_1, VALID_START_PFN_1 + 2,
	    VALID_AVAIL_START_PFN_1 + 1, VALID_AVAIL_START_PFN_1 + 2,
	    VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* Insert more than one segment iff VM_PHYSSEG_MAX > 1 */
#if VM_PHYSSEG_MAX > 1
	uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());
#endif

#if VM_PHYSSEG_MAX == 1
	atf_tc_expect_signal(SIGABRT,
	    "cannot uvm_page_physunload() the last segment");
#endif

	ATF_CHECK_EQ(true, uvm_page_physunload(upm, VM_FREELIST_DEFAULT, &p));

	p = 0;

	ATF_CHECK_EQ(true, uvm_page_physunload(upm, VM_FREELIST_DEFAULT, &p));

	ATF_CHECK_EQ(VALID_START_PFN_1 + 2, atop(p));

	ATF_CHECK_EQ(1, uvm_physseg_get_entries());

	/* The only node now is the one we inserted second. */
	upm = uvm_physseg_get_first();

	ATF_CHECK_EQ(VALID_START_PFN_2, uvm_physseg_get_start(upm));
	ATF_CHECK_EQ(VALID_END_PFN_2, uvm_physseg_get_end(upm));
	ATF_CHECK_EQ(VALID_AVAIL_START_PFN_2, uvm_physseg_get_avail_start(upm));
	ATF_CHECK_EQ(VALID_AVAIL_END_PFN_2, uvm_physseg_get_avail_end(upm));
}

ATF_TC(uvm_page_physunload_invalid);
ATF_TC_HEAD(uvm_page_physunload_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the  uvm_page_physunload() \
	    fails when then Free list does not match.");
}
ATF_TC_BODY(uvm_page_physunload_invalid, tc)
{
	psize_t npages = (VALID_END_PFN_2 - VALID_START_PFN_2);

	struct vm_page *pgs = malloc(sizeof(struct vm_page) * npages);

	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	uvm_physseg_init_seg(upm, pgs);

	ATF_CHECK_EQ(false, uvm_page_physunload(upm, VM_FREELIST_FIRST4G, &p));
}

ATF_TC(uvm_page_physunload_force);
ATF_TC_HEAD(uvm_page_physunload_force, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the basic \
	    uvm_page_physunload_force() including delete works without.");
}
ATF_TC_BODY(uvm_page_physunload_force, tc)
{
	/*
	 * Would uvmexp.npages reduce everytime an uvm_page_physunload is called?
	 */
	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_1, VALID_END_PFN_1,
	    VALID_AVAIL_START_PFN_1, VALID_AVAIL_END_PFN_1, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	/* Insert more than one segment iff VM_PHYSSEG_MAX > 1 */
#if VM_PHYSSEG_MAX > 1
	/*
	 * We have couple of physloads done this is bacause of the fact that if
	 * we physunload all the PFs from a given range and we have only one
	 * segment in total a panic() is called
	 */
	uvm_page_physload(VALID_START_PFN_2, VALID_END_PFN_2,
	    VALID_AVAIL_START_PFN_2, VALID_AVAIL_END_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(2, uvm_physseg_get_entries());
#endif

#if VM_PHYSSEG_MAX == 1
	atf_tc_expect_signal(SIGABRT,
	    "cannot uvm_page_physunload() the last segment");
#endif

	ATF_REQUIRE_EQ(VALID_AVAIL_START_PFN_1,
	    uvm_physseg_get_avail_start(upm));

	for(paddr_t i = VALID_AVAIL_START_PFN_1;
	    i < VALID_AVAIL_END_PFN_1; i++) {
		ATF_CHECK_EQ(true,
		    uvm_page_physunload_force(upm, VM_FREELIST_DEFAULT, &p));
		ATF_CHECK_EQ(i, atop(p));

		if(i + 1 < VALID_AVAIL_END_PFN_1)
			ATF_CHECK_EQ(i + 1, uvm_physseg_get_avail_start(upm));
	}

	/*
	 * Now we try to retrieve the segment, which has been removed
	 * from the system through force unloading all the pages inside it.
	 */
	upm = uvm_physseg_find(VALID_AVAIL_END_PFN_1 - 1, NULL);

	/* It should no longer exist */
	ATF_CHECK_EQ(NULL, upm);

	ATF_CHECK_EQ(1, uvm_physseg_get_entries());
}

ATF_TC(uvm_page_physunload_force_invalid);
ATF_TC_HEAD(uvm_page_physunload_force_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests if the invalid conditions for \
	    uvm_page_physunload_force_invalid().");
}
ATF_TC_BODY(uvm_page_physunload_force_invalid, tc)
{
	paddr_t p = 0;

	uvm_physseg_t upm;

	setup();
	upm = uvm_page_physload(VALID_START_PFN_2, VALID_START_PFN_2+ 1,
	    VALID_START_PFN_2, VALID_START_PFN_2, VM_FREELIST_DEFAULT);

	ATF_REQUIRE_EQ(1, uvm_physseg_get_entries());

	ATF_REQUIRE_EQ(0, uvmexp.npages);

	ATF_CHECK_EQ(false,
	    uvm_page_physunload_force(upm, VM_FREELIST_DEFAULT, &p));

	ATF_CHECK_EQ(0, p);
}

ATF_TP_ADD_TCS(tp)
{
#if defined(UVM_HOTPLUG)
	/* Internal */
	ATF_TP_ADD_TC(tp, uvm_physseg_alloc_atboot_mismatch);
	ATF_TP_ADD_TC(tp, uvm_physseg_alloc_atboot_overrun);
	ATF_TP_ADD_TC(tp, uvm_physseg_alloc_sanity);
	ATF_TP_ADD_TC(tp, uvm_physseg_free_atboot_mismatch);
	ATF_TP_ADD_TC(tp, uvm_physseg_free_sanity);
#if VM_PHYSSEG_MAX > 1
	ATF_TP_ADD_TC(tp, uvm_physseg_atboot_free_leak);
#endif
#endif /* UVM_HOTPLUG */

	ATF_TP_ADD_TC(tp, uvm_physseg_plug);
	ATF_TP_ADD_TC(tp, uvm_physseg_unplug);

	/* Exported */
	ATF_TP_ADD_TC(tp, uvm_physseg_init);
	ATF_TP_ADD_TC(tp, uvm_page_physload_preload);
	ATF_TP_ADD_TC(tp, uvm_page_physload_postboot);
	ATF_TP_ADD_TC(tp, uvm_physseg_handle_immutable);
	ATF_TP_ADD_TC(tp, uvm_physseg_seg_chomp_slab);
	ATF_TP_ADD_TC(tp, uvm_physseg_alloc_from_slab);
	ATF_TP_ADD_TC(tp, uvm_physseg_init_seg);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_start);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_start_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_end);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_end_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_avail_start);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_avail_start_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_avail_end);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_avail_end_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_next);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_next_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_prev);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_prev_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_first);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_last);
	ATF_TP_ADD_TC(tp, uvm_physseg_valid);
	ATF_TP_ADD_TC(tp, uvm_physseg_valid_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_highest);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_free_list);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_start_hint);
	ATF_TP_ADD_TC(tp, uvm_physseg_set_start_hint);
	ATF_TP_ADD_TC(tp, uvm_physseg_set_start_hint_invalid);
	ATF_TP_ADD_TC(tp, uvm_physseg_get_pg);

#ifdef __HAVE_PMAP_PHYSSEG
	ATF_TP_ADD_TC(tp, uvm_physseg_get_pmseg);
#endif
	ATF_TP_ADD_TC(tp, vm_physseg_find);
	ATF_TP_ADD_TC(tp, vm_physseg_find_invalid);

	ATF_TP_ADD_TC(tp, uvm_page_physunload_start);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_end);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_none);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_delete_start);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_delete_end);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_invalid);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_force);
	ATF_TP_ADD_TC(tp, uvm_page_physunload_force_invalid);

	return atf_no_error();
}
