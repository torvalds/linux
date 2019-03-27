/* $Header: /p/tcsh/cvsroot/tcsh/tc.alloc.c,v 3.56 2016/03/08 12:47:43 christos Exp $ */
/*
 * tc.alloc.c (Caltech) 2/21/82
 * Chris Kingsley, kingsley@cit-20.
 *
 * This is a very fast storage allocator.  It allocates blocks of a small
 * number of different sizes, and keeps free lists of each size.  Blocks that
 * don't exactly fit are passed up to the next larger size.  In this
 * implementation, the available sizes are 2^n-4 (or 2^n-12) bytes long.
 * This is designed for use in a program that uses vast quantities of memory,
 * but bombs when it runs out.
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"
#ifdef HAVE_MALLINFO
#include <malloc.h>
#endif
#if defined(HAVE_SBRK) && !defined(__APPLE__)
#define USE_SBRK
#endif

RCSID("$tcsh: tc.alloc.c,v 3.56 2016/03/08 12:47:43 christos Exp $")

#define RCHECK
#define DEBUG

static char   *memtop = NULL;		/* PWP: top of current memory */
static char   *membot = NULL;		/* PWP: bottom of allocatable memory */

int dont_free = 0;

#ifdef WINNT_NATIVE
# define malloc		fmalloc
# define free		ffree
# define calloc		fcalloc
# define realloc	frealloc
#endif /* WINNT_NATIVE */

#if !defined(DEBUG) || defined(SYSMALLOC)
static void
out_of_memory (void)
{
    static const char msg[] = "Out of memory\n";

    TCSH_IGNORE(write(didfds ? 2 : SHDIAG, msg, strlen(msg)));
    _exit(1);
}
#endif

#ifndef SYSMALLOC

#ifdef SX
extern void* sbrk();
#endif
/*
 * Lots of os routines are busted and try to free invalid pointers. 
 * Although our free routine is smart enough and it will pick bad 
 * pointers most of the time, in cases where we know we are going to get
 * a bad pointer, we'd rather leak.
 */

#ifndef NULL
#define	NULL 0
#endif

typedef unsigned char U_char;	/* we don't really have signed chars */
typedef unsigned int U_int;
typedef unsigned short U_short;
typedef unsigned long U_long;


/*
 * The overhead on a block is at least 4 bytes.  When free, this space
 * contains a pointer to the next free block, and the bottom two bits must
 * be zero.  When in use, the first byte is set to MAGIC, and the second
 * byte is the size index.  The remaining bytes are for alignment.
 * If range checking is enabled and the size of the block fits
 * in two bytes, then the top two bytes hold the size of the requested block
 * plus the range checking words, and the header word MINUS ONE.
 */


#define MEMALIGN(a) (((a) + ROUNDUP) & ~ROUNDUP)

union overhead {
    union overhead *ov_next;	/* when free */
    struct {
	U_char  ovu_magic;	/* magic number */
	U_char  ovu_index;	/* bucket # */
#ifdef RCHECK
	U_short ovu_size;	/* actual block size */
	U_int   ovu_rmagic;	/* range magic number */
#endif
    }       ovu;
#define	ov_magic	ovu.ovu_magic
#define	ov_index	ovu.ovu_index
#define	ov_size		ovu.ovu_size
#define	ov_rmagic	ovu.ovu_rmagic
};

#define	MAGIC		0xfd	/* magic # on accounting info */
#define RMAGIC		0x55555555	/* magic # on range info */
#ifdef RCHECK
#define	RSLOP		sizeof (U_int)
#else
#define	RSLOP		0
#endif


#ifdef _LP64
#define ROUNDUP	15
#else
#define ROUNDUP	7
#endif

/*
 * nextf[i] is the pointer to the next free block of size 2^(i+3).  The
 * smallest allocatable block is 8 bytes.  The overhead information
 * precedes the data area returned to the user.
 */
#define	NBUCKETS ((sizeof(long) << 3) - 3)
static union overhead *nextf[NBUCKETS] IZERO_STRUCT;

/*
 * nmalloc[i] is the difference between the number of mallocs and frees
 * for a given block size.
 */
static U_int nmalloc[NBUCKETS] IZERO_STRUCT;

#ifndef lint
static	int	findbucket	(union overhead *, int);
static	void	morecore	(int);
#endif


#ifdef DEBUG
# define CHECK(a, str, p) \
    if (a) { \
	xprintf(str, p);	\
	xprintf(" (memtop = %p membot = %p)\n", memtop, membot);	\
	abort(); \
    }
#else
# define CHECK(a, str, p) \
    if (a) { \
	xprintf(str, p);	\
	xprintf(" (memtop = %p membot = %p)\n", memtop, membot);	\
	return; \
    }
#endif

memalign_t
malloc(size_t nbytes)
{
#ifndef lint
    union overhead *p;
    int bucket = 0;
    unsigned shiftr;

    /*
     * Convert amount of memory requested into closest block size stored in
     * hash buckets which satisfies request.  Account for space used per block
     * for accounting.
     */
#ifdef SUNOS4
    /*
     * SunOS localtime() overwrites the 9th byte on an 8 byte malloc()....
     * so we get one more...
     * From Michael Schroeder: This is not true. It depends on the 
     * timezone string. In Europe it can overwrite the 13th byte on a
     * 12 byte malloc.
     * So we punt and we always allocate an extra byte.
     */
    nbytes++;
#endif

    nbytes = MEMALIGN(MEMALIGN(sizeof(union overhead)) + nbytes + RSLOP);
    shiftr = (nbytes - 1) >> 2;

    /* apart from this loop, this is O(1) */
    while ((shiftr >>= 1) != 0)
	bucket++;
    /*
     * If nothing in hash bucket right now, request more memory from the
     * system.
     */
    if (nextf[bucket] == NULL)
	morecore(bucket);
    if ((p = nextf[bucket]) == NULL) {
	child++;
#ifndef DEBUG
	out_of_memory();
#else
	showall(NULL, NULL);
	xprintf(CGETS(19, 1, "nbytes=%zu: Out of memory\n"), nbytes);
	abort();
#endif
	/* fool lint */
	return ((memalign_t) 0);
    }
    /* remove from linked list */
    nextf[bucket] = nextf[bucket]->ov_next;
    p->ov_magic = MAGIC;
    p->ov_index = bucket;
    nmalloc[bucket]++;
#ifdef RCHECK
    /*
     * Record allocated size of block and bound space with magic numbers.
     */
    p->ov_size = (p->ov_index <= 13) ? nbytes - 1 : 0;
    p->ov_rmagic = RMAGIC;
    *((U_int *) (((caddr_t) p) + nbytes - RSLOP)) = RMAGIC;
#endif
    return ((memalign_t) (((caddr_t) p) + MEMALIGN(sizeof(union overhead))));
#else
    if (nbytes)
	return ((memalign_t) 0);
    else
	return ((memalign_t) 0);
#endif /* !lint */
}

#ifndef lint
/*
 * Allocate more memory to the indicated bucket.
 */
static void
morecore(int bucket)
{
    union overhead *op;
    int rnu;		/* 2^rnu bytes will be requested */
    int nblks;		/* become nblks blocks of the desired size */
    int siz;

    if (nextf[bucket])
	return;
    /*
     * Insure memory is allocated on a page boundary.  Should make getpageize
     * call?
     */
    op = (union overhead *) sbrk(0);
    memtop = (char *) op;
    if (membot == NULL)
	membot = memtop;
    if ((long) op & 0x3ff) {
	memtop = sbrk((int) (1024 - ((long) op & 0x3ff)));
	memtop += (long) (1024 - ((long) op & 0x3ff));
    }

    /* take 2k unless the block is bigger than that */
    rnu = (bucket <= 8) ? 11 : bucket + 3;
    nblks = 1 << (rnu - (bucket + 3));	/* how many blocks to get */
    memtop = sbrk(1 << rnu);	/* PWP */
    op = (union overhead *) memtop;
    /* no more room! */
    if ((long) op == -1)
	return;
    memtop += (long) (1 << rnu);
    /*
     * Round up to minimum allocation size boundary and deduct from block count
     * to reflect.
     */
    if (((U_long) op) & ROUNDUP) {
	op = (union overhead *) (((U_long) op + (ROUNDUP + 1)) & ~ROUNDUP);
	nblks--;
    }
    /*
     * Add new memory allocated to that on free list for this hash bucket.
     */
    nextf[bucket] = op;
    siz = 1 << (bucket + 3);
    while (--nblks > 0) {
	op->ov_next = (union overhead *) (((caddr_t) op) + siz);
	op = (union overhead *) (((caddr_t) op) + siz);
    }
    op->ov_next = NULL;
}

#endif

void
free(ptr_t cp)
{
#ifndef lint
    int size;
    union overhead *op;

    /*
     * the don't free flag is there so that we avoid os bugs in routines
     * that free invalid pointers!
     */
    if (cp == NULL || dont_free)
	return;
    CHECK(!memtop || !membot,
	  CGETS(19, 2, "free(%p) called before any allocations."), cp);
    CHECK(cp > (ptr_t) memtop,
	  CGETS(19, 3, "free(%p) above top of memory."), cp);
    CHECK(cp < (ptr_t) membot,
	  CGETS(19, 4, "free(%p) below bottom of memory."), cp);
    op = (union overhead *) (((caddr_t) cp) - MEMALIGN(sizeof(union overhead)));
    CHECK(op->ov_magic != MAGIC,
	  CGETS(19, 5, "free(%p) bad block."), cp);

#ifdef RCHECK
    if (op->ov_index <= 13)
	CHECK(*(U_int *) ((caddr_t) op + op->ov_size + 1 - RSLOP) != RMAGIC,
	      CGETS(19, 6, "free(%p) bad range check."), cp);
#endif
    CHECK(op->ov_index >= NBUCKETS,
	  CGETS(19, 7, "free(%p) bad block index."), cp);
    size = op->ov_index;
    op->ov_next = nextf[size];
    nextf[size] = op;

    nmalloc[size]--;

#else
    if (cp == NULL)
	return;
#endif
}

memalign_t
calloc(size_t i, size_t j)
{
#ifndef lint
    char *cp;
    volatile size_t k;

    i *= j;
    cp = xmalloc(i);
    /* Stop gcc 5.x from optimizing malloc+memset = calloc */
    k = i;
    memset(cp, 0, k);

    return ((memalign_t) cp);
#else
    if (i && j)
	return ((memalign_t) 0);
    else
	return ((memalign_t) 0);
#endif
}

/*
 * When a program attempts "storage compaction" as mentioned in the
 * old malloc man page, it realloc's an already freed block.  Usually
 * this is the last block it freed; occasionally it might be farther
 * back.  We have to search all the free lists for the block in order
 * to determine its bucket: 1st we make one pass thru the lists
 * checking only the first block in each; if that fails we search
 * ``realloc_srchlen'' blocks in each list for a match (the variable
 * is extern so the caller can modify it).  If that fails we just copy
 * however many bytes was given to realloc() and hope it's not huge.
 */
#ifndef lint
/* 4 should be plenty, -1 =>'s whole list */
static int     realloc_srchlen = 4;	
#endif /* lint */

memalign_t
realloc(ptr_t cp, size_t nbytes)
{
#ifndef lint
    U_int onb;
    union overhead *op;
    ptr_t res;
    int i;
    int     was_alloced = 0;

    if (cp == NULL)
	return (malloc(nbytes));
    op = (union overhead *) (((caddr_t) cp) - MEMALIGN(sizeof(union overhead)));
    if (op->ov_magic == MAGIC) {
	was_alloced++;
	i = op->ov_index;
    }
    else
	/*
	 * Already free, doing "compaction".
	 * 
	 * Search for the old block of memory on the free list.  First, check the
	 * most common case (last element free'd), then (this failing) the last
	 * ``realloc_srchlen'' items free'd. If all lookups fail, then assume
	 * the size of the memory block being realloc'd is the smallest
	 * possible.
	 */
	if ((i = findbucket(op, 1)) < 0 &&
	    (i = findbucket(op, realloc_srchlen)) < 0)
	    i = 0;

    onb = MEMALIGN(nbytes + MEMALIGN(sizeof(union overhead)) + RSLOP);

    /* avoid the copy if same size block */
    if (was_alloced && (onb <= (U_int) (1 << (i + 3))) && 
	(onb > (U_int) (1 << (i + 2)))) {
#ifdef RCHECK
	/* JMR: formerly this wasn't updated ! */
	nbytes = MEMALIGN(MEMALIGN(sizeof(union overhead))+nbytes+RSLOP);
	*((U_int *) (((caddr_t) op) + nbytes - RSLOP)) = RMAGIC;
	op->ov_rmagic = RMAGIC;
	op->ov_size = (op->ov_index <= 13) ? nbytes - 1 : 0;
#endif
	return ((memalign_t) cp);
    }
    if ((res = malloc(nbytes)) == NULL)
	return ((memalign_t) NULL);
    if (cp != res) {		/* common optimization */
	/* 
	 * christos: this used to copy nbytes! It should copy the 
	 * smaller of the old and new size
	 */
	onb = (1 << (i + 3)) - MEMALIGN(sizeof(union overhead)) - RSLOP;
	(void) memmove(res, cp, onb < nbytes ? onb : nbytes);
    }
    if (was_alloced)
	free(cp);
    return ((memalign_t) res);
#else
    if (cp && nbytes)
	return ((memalign_t) 0);
    else
	return ((memalign_t) 0);
#endif /* !lint */
}

/*
 * On linux, _nss_nis_setnetgrent() calls this function to determine
 * the usable size of the pointer passed, but this is not a portable
 * API, so we cannot use our malloc replacement without providing one.
 * Thanks a lot glibc!
 */
#ifdef __linux__
#define M_U_S_CONST
#else
#define M_U_S_CONST
#endif
size_t malloc_usable_size(M_U_S_CONST void *);
size_t
malloc_usable_size(M_U_S_CONST void *ptr)
{
    const union overhead *op = (const union overhead *)
	(((const char *) ptr) - MEMALIGN(sizeof(*op)));
    if (op->ov_magic == MAGIC)
	    return 1 << (op->ov_index + 3);
    else
	    return 0;
}


#ifndef lint
/*
 * Search ``srchlen'' elements of each free list for a block whose
 * header starts at ``freep''.  If srchlen is -1 search the whole list.
 * Return bucket number, or -1 if not found.
 */
static int
findbucket(union overhead *freep, int srchlen)
{
    union overhead *p;
    size_t i;
    int j;

    for (i = 0; i < NBUCKETS; i++) {
	j = 0;
	for (p = nextf[i]; p && j != srchlen; p = p->ov_next) {
	    if (p == freep)
		return (i);
	    j++;
	}
    }
    return (-1);
}

#endif


#else				/* SYSMALLOC */

/**
 ** ``Protected versions'' of malloc, realloc, calloc, and free
 **
 ** On many systems:
 **
 ** 1. malloc(0) is bad
 ** 2. free(0) is bad
 ** 3. realloc(0, n) is bad
 ** 4. realloc(n, 0) is bad
 **
 ** Also we call our error routine if we run out of memory.
 **/
memalign_t
smalloc(size_t n)
{
    ptr_t   ptr;

    n = n ? n : 1;

#ifdef USE_SBRK
    if (membot == NULL)
	membot = sbrk(0);
#endif /* USE_SBRK */

    if ((ptr = malloc(n)) == NULL)
	out_of_memory();
#ifndef USE_SBRK
    if (memtop < ((char *) ptr) + n)
	memtop = ((char *) ptr) + n;
    if (membot == NULL)
	membot = ptr;
#endif /* !USE_SBRK */
    return ((memalign_t) ptr);
}

memalign_t
srealloc(ptr_t p, size_t n)
{
    ptr_t   ptr;

    n = n ? n : 1;

#ifdef USE_SBRK
    if (membot == NULL)
	membot = sbrk(0);
#endif /* USE_SBRK */

    if ((ptr = (p ? realloc(p, n) : malloc(n))) == NULL)
	out_of_memory();
#ifndef USE_SBRK
    if (memtop < ((char *) ptr) + n)
	memtop = ((char *) ptr) + n;
    if (membot == NULL)
	membot = ptr;
#endif /* !USE_SBRK */
    return ((memalign_t) ptr);
}

memalign_t
scalloc(size_t s, size_t n)
{
    ptr_t   ptr;

    n *= s;
    n = n ? n : 1;

#ifdef USE_SBRK
    if (membot == NULL)
	membot = sbrk(0);
#endif /* USE_SBRK */

    if ((ptr = malloc(n)) == NULL)
	out_of_memory();

    memset (ptr, 0, n);

#ifndef USE_SBRK
    if (memtop < ((char *) ptr) + n)
	memtop = ((char *) ptr) + n;
    if (membot == NULL)
	membot = ptr;
#endif /* !USE_SBRK */

    return ((memalign_t) ptr);
}

void
sfree(ptr_t p)
{
    if (p && !dont_free)
	free(p);
}

#endif /* SYSMALLOC */

/*
 * mstats - print out statistics about malloc
 *
 * Prints two lines of numbers, one showing the length of the free list
 * for each size category, the second showing the number of mallocs -
 * frees for each size category.
 */
/*ARGSUSED*/
void
showall(Char **v, struct command *c)
{
#ifndef SYSMALLOC
    size_t i, j;
    union overhead *p;
    int     totfree = 0, totused = 0;

    xprintf(CGETS(19, 8, "%s current memory allocation:\nfree:\t"), progname);
    for (i = 0; i < NBUCKETS; i++) {
	for (j = 0, p = nextf[i]; p; p = p->ov_next, j++)
	    continue;
	xprintf(" %4zd", j);
	totfree += j * (1 << (i + 3));
    }
    xprintf("\n%s:\t", CGETS(19, 9, "used"));
    for (i = 0; i < NBUCKETS; i++) {
	xprintf(" %4d", nmalloc[i]);
	totused += nmalloc[i] * (1 << (i + 3));
    }
    xprintf(CGETS(19, 10, "\n\tTotal in use: %d, total free: %d\n"),
	    totused, totfree);
    xprintf(CGETS(19, 11,
	    "\tAllocated memory from 0x%lx to 0x%lx.  Real top at 0x%lx\n"),
	    (unsigned long) membot, (unsigned long) memtop,
	    (unsigned long) sbrk(0));
#else /* SYSMALLOC */
#ifndef HAVE_MALLINFO
#ifdef USE_SBRK
    memtop = sbrk(0);
#endif /* USE_SBRK */
    xprintf(CGETS(19, 12, "Allocated memory from 0x%lx to 0x%lx (%ld).\n"),
	    (unsigned long) membot, (unsigned long) memtop, 
	    (unsigned long) (memtop - membot));
#else /* HAVE_MALLINFO */
    struct mallinfo mi;

    mi = mallinfo();
    xprintf(CGETS(19, 13, "%s current memory allocation:\n"), progname);
    xprintf(CGETS(19, 14, "Total space allocated from system: %d\n"), mi.arena);
    xprintf(CGETS(19, 15, "Number of non-inuse chunks: %d\n"), mi.ordblks);
    xprintf(CGETS(19, 16, "Number of mmapped regions: %d\n"), mi.hblks);
    xprintf(CGETS(19, 17, "Total space in mmapped regions: %d\n"), mi.hblkhd);
    xprintf(CGETS(19, 18, "Total allocated space: %d\n"), mi.uordblks);
    xprintf(CGETS(19, 19, "Total non-inuse space: %d\n"), mi.fordblks);
    xprintf(CGETS(19, 20, "Top-most, releasable space: %d\n"), mi.keepcost);
#endif /* HAVE_MALLINFO */
#endif /* SYSMALLOC */
    USE(c);
    USE(v);
}
